# Bitchat-over-MeshCore Tunnel — Slice Plan

Status: draft, nothing built yet. Branch: `feature/bitchat-tunnel`.

## Goal

Private Bitchat DMs travel end-to-end over a MeshCore LoRa link. Bridges carry
opaque frames and never hold Noise keys or plaintext. Phone apps and MeshCore
repeaters stay unmodified.

```
Phone A ──BLE──> Bridge A ══LoRa══> Bridge B ──BLE──> Phone B
   \______________ Noise session (bridges never decrypt) ______________/
```

## Ground rules

1. **One new variable per slice.** Each slice adds exactly one thing that can
   break, on top of a slice that already passed. If a slice fails, the cause is
   in the new part.
2. **Every slice has a control.** We run the same test with the new thing
   "turned off" (zero delay, passthrough, etc.) and it must pass first.
3. **Tests are end-to-end runs with a written pass/fail line**, recorded in
   `docs/tunnel-results.md` (date, firmware commit, phone/app version, result).
   The only automated tests are small host-side checks for the fragmenter
   (Slice 3). No large unit-test suites.
4. **Additive only.** The existing `#mesh` bridge keeps working in every slice.
   Each slice re-checks it (the "regression check" below).

**Regression check (every slice, ~1 min):** the phone sees the bridge peer and
a `#mesh` message from the phone shows up in the bridge's serial log as relayed.

## Hardware on hand and what it unlocks

| Have | Unlocks |
|---|---|
| 1× Cardputer-Adv + Cap LoRa-1262, 1× phone, a laptop | Slices 0–3 |
| + a second bridge radio | Slice 4 (real LoRa) |
| + a second phone | Slice 5 (true phone-to-phone) |
| + a third radio running stock repeater firmware, or a nearby public repeater | Slice 6 (multi-hop) |

Until the second radio arrives, a **virtual remote peer** stands in for the far
side of the link. It's a Python script on the laptop, connected to the Cardputer
over USB serial. It plays a remote Bitchat phone with its own keys, and later
also plays the remote bridge. That lets us answer the biggest question (will
the apps tolerate LoRa-sized delays?) with the hardware you already have.

### Rough cost to finish (estimates, check prices at purchase)

| Item | Purpose | Est. |
|---|---|---|
| Second Cardputer Mesh Kit (same hardware, fewest variables) **or** Heltec V3 (already a supported bridge target, cheaper) | Bridge B | ~$25–80 |
| Used Android phone that runs Bitchat | Phone B | ~$50–100 |
| Optional third radio (Heltec V3) with stock repeater firmware | Multi-hop test | ~$25 |

## Facts the plan relies on (from source, 2026-10)

| Fact | Where | Why it matters |
|---|---|---|
| Repeaters never flood `RAW_CUSTOM`; they only forward it along a known direct path, and it has no destination field | `src/Mesh.cpp:292-297`, `:79-107` | Tunnel uses MeshCore **contact datagrams** between the two bridges (flood first, then a learned direct path with ACKs) |
| Bitchat signature is computed with TTL forced to 0 | `BitchatBridge.cpp:1037-1040` | Bridges may lower TTL without breaking signatures |
| Android: Noise handshake expires if no progress for **10 s**; retries back off 5/15/30/60 s | `NoiseSessionManager.kt:55`, `AppConstants.kt:139` | Each handshake round trip over LoRa must finish in under 10 s |
| Android: peer dropped after **3 min** without being seen | `AppConstants.kt:15` | A remote peer's announce must cross LoRa about every 2–2.5 min |
| Apps reject announces more than 10 min off wall clock; the timestamp is signed | bridge docs | Bridges can't refresh old announces, only relay fresh ones |
| Bridge parser caps payload at 245 B and decompresses | `BitchatProtocol.h:11` | Tunnel needs a raw-bytes tap before parsing |
| iOS equivalents | **TBD, Slice 1** | Needed only if a phone is iOS |

Airtime per LoRa packet at the US preset (910.525 MHz, SF7, BW 62.5, CR 4/5):
~240 ms at 60 B, ~650 ms at 200 B, ~820 ms at 255 B.

---

## Slice 0 — Cardputer runs the existing bridge (no new logic)

**Adds:** a `M5stack_cardputer_cap_lora1262_companion_radio_usb_bitchat` build
target in this fork. It's ported from `meshcore-cardputer-adv` (board JSON, pins,
`M5CardputerBoard`), runs headless with the serial log only, and uses the US
radio preset.
Not ported: the keyboard/LVGL UI (adds variables, not needed).

**Control:** the same firmware tree builds for `Heltec_WSL3_..._usb_bitchat`
(known good) before and after the change.

**Test (E2E):**
1. Flash, open the USB serial monitor, `BITCHAT_DEBUG=1`.
2. Open Bitchat on the phone near the Cardputer.
3. Send `#mesh` "s0 hello" from the phone.

**Pass when:**
- [ ] The bridge appears in the phone's peer list within 30 s and stays for 10 min.
- [ ] The serial log shows the `#mesh` message relayed to MeshCore.
- [ ] The serial log shows the radio initialised at 910.525 / 62.5 / SF7 with no errors.

**If it fails:** it's the port (pins, power-enable via the I2C expander at
`0x43`, partitions). There's no tunnel code yet.

---

## Slice 1 — Go/no-go: do the apps tolerate LoRa delays? (no LoRa yet)

This slice decides whether the project continues as designed.

**Adds:**
- Firmware, behind the build flag `BITCHAT_RAW_TAP=1`:
  - Every raw BLE frame from the phone is written to serial as `RAW< <hex>`.
  - Any `RAW> <hex>` line from serial is sent to the phone byte-for-byte via BLE
    notify.
  - The normal bridge logic still runs. This build disables the MeshCore companion
    protocol on USB, so serial is text only.
- Laptop: `tools/tunnel/virtual_peer.py`, a remote Bitchat peer.
  - Its own Ed25519 and X25519 keys; peer ID = first 8 bytes of SHA-256 of the
    Noise static key.
  - Sends signed announces with TTL one below max, so it looks relayed through
    the bridge, exactly like a real remote peer will.
  - Noise XX responder and initiator (`Noise_XX_25519_ChaChaPoly_SHA256`).
  - Auto-replies `echo: <text>` to DMs.
  - `--delay S` holds every frame in both directions for S seconds.

Sub-steps, one variable each:

| Step | What's new | Pass when |
|---|---|---|
| 1a | Raw tap only (no Python peer) | Phone frames appear as `RAW<` lines; regression check passes |
| 1b | Python peer announces, delay 0 | "virtual-peer" shows in the phone's peer list and stays ≥10 min |
| 1c | DM, delay 0 (the control) | Phone DMs "1c"; gets "echo: 1c"; the app shows it as encrypted/verified |
| 1d | Delay sweep 1, 3, 6, 9, 12 s | Record per delay: handshake ok?, DM ok?, time to echo |
| 1e | Announce interval sweep 60, 120, 150, 180 s | Record: does the peer stay listed? |

**Output (go/no-go), written to `docs/tunnel-results.md`:**
- Largest one-way delay at which handshake + DM still succeed.
- Longest announce interval that keeps the remote peer visible.
- **Go** if the handshake works at ≥ 3 s one-way, since one hop at SF7 needs
  about 1–2 s. **No-go** means the unmodified apps can't do this, and we pause to
  decide on app changes.

**Also captured here (needed for Slice 3):** real frame sizes for announce,
handshake messages 1/2/3, a short DM, a delivery ack and a read receipt, taken
from the `RAW<` lines.

**If 1c fails at zero delay:** the bug is in `virtual_peer.py`, not the concept.
Compare its frames against the phone's own frames byte-for-byte.

---

## Slice 2 — Bridge decides what to tunnel (still serial, no LoRa)

**Adds:** the bridge-side routing logic, replacing the "send everything" tap.
- **Remote peer table:** learned from announces that arrive *from the tunnel*
  (peer ID → last seen, expiry 2× the announce interval).
- **BLE → tunnel:** only frames whose recipient is in the remote table, plus
  local announces (rate limited per peer to the Slice 1e interval).
- **Tunnel → BLE:** frames go to the phone verbatim, with TTL clamped to 1.
- **Logs:** counts only (frames in/out, dropped, why); no peer IDs unless
  `BITCHAT_DEBUG` is set.

**Control:** rerun Slice 1c at delay 0. Same pass criteria; only the filter is
new.

**Pass when:**
- [ ] 1b and 1c pass again with the filter on.
- [ ] Public `#mesh` traffic and announces from peers not in the remote table
  produce **no** tunnel output (check the serial counters).
- [ ] The remote peer disappears from the table after its expiry when
  `virtual_peer.py` is stopped.

---

## Slice 3 — Tunnel frame format + fragmentation (still serial, no LoRa)

**Adds:** the on-air format, exercised over serial first so it can be inspected.

```
byte 0    : 0xB0 | version (1)
byte 1    : flags  bit0 = last fragment
bytes 2-3 : msg_id (random per Bitchat frame)
byte 4    : frag_index
byte 5    : frag_count
bytes 6.. : fragment data (≤ chunk size, set from the Slice 4 datagram limit)
```

- The reassembler has a fixed pool (4 messages), a 30 s timeout, and drops
  duplicates and bad indices.
- No compression (Noise ciphertext doesn't compress) and no padding stripping
  yet. Stripping padding is a separate later step only if airtime demands it.
- The Python peer speaks the same format.
  `virtual_peer.py --chunk N --drop-frag K` lets us force fragmentation and loss.
- **Host checks:** one small script (`tools/tunnel/frag_check.py`) round-trips the
  Slice 1 captured frames through a Python mirror of the fragmenter, with
  reorder, duplicate and loss cases. That's the only automated test file.

**Control:** `--chunk 512` (no fragmentation) must pass 1c before smaller chunks
are tried.

**Pass when:**
- [ ] 1c passes with `--chunk 150` (handshake msg 2 and DMs then span 2 fragments).
- [ ] With `--drop-frag 1`, the DM is not delivered, the app's own retry
  eventually succeeds, and the bridge pool doesn't leak (count returns to 0
  after the timeout).
- [ ] 200 DMs in a row pass with free heap stable (logged every 50).

---

## Slice 4 — Real LoRa, one hop (needs Bridge B)

**Adds:** MeshCore transport between two bridges. Bridge B runs the same
firmware; its serial hosts `virtual_peer.py` in raw-tap mode (no second phone
yet).

```
Phone ─BLE─ Cardputer ══LoRa (0 hops)══ Bridge B ─USB─ virtual_peer.py
```

- The bridges are MeshCore contacts of each other: exchange adverts once, pinned
  in config.
- Tunnel fragments are sent as contact datagrams: flood on first send, direct
  path after the path return, MeshCore ACK per fragment.
- One config knob only: `tunnel.peer <pubkey prefix>`. Other CLI settings come
  later.

**Control:** the plain MeshCore DM between the two bridges (stock companion
feature) works before any tunnel traffic.

**Pass when:**
- [ ] The phone sees virtual-peer through LoRa and keeps seeing it for 30 min.
- [ ] Handshake + 20 DMs succeed. Record time to echo and airtime per DM
  (from MeshCore's TX logs).
- [ ] The regression check passes on both bridges.

---

## Slice 5 — Phone to phone (needs Phone B)

**Adds:** a real phone behind Bridge B instead of `virtual_peer.py`. Bridge B
switches from raw-tap serial to its BLE.

**Pass when:**
- [ ] Each phone sees the other.
- [ ] DMs go both ways; delivery acks arrive; read receipts arrive (or are
  knowingly dropped).
- [ ] Mixed iOS/Android if available; record app versions.

---

## Slice 6 — Multi-hop and loss (needs a stock repeater)

**Adds:** one unmodified repeater between the bridges (low TX power or distance
to force the hop).

**Pass when:**
- [ ] Slice 5 checks pass through the repeater.
- [ ] Results table for 0 vs 1 hop: handshake time, DM latency, airtime per DM.

---

## Deferred (after Slice 6, only if the measurements call for it)

- Padding stripping (Slice 1 decides whether it's needed).
- Airtime budget / priority queue, `tunnel.*` CLI, `get tunnel.peers`.
- Dropping read receipts to save airtime.
- Bridge-to-bridge authentication of relayed announces.
- Release builds for other boards; upstream PR to jooray.

## Threat-model notes (to expand into `docs/tunnel.md`)

- Bridges and RF observers see which bridge talks to which, when, and how big
  the frames are. They never see content or Noise keys.
- MeshCore's own encryption on the contact datagram is defence-in-depth only;
  privacy comes from Noise.
- 915 MHz ISM (Part 15). Encryption is fine there; not for ham bands.

## Open questions for the human

1. Phone OS (iOS or Android) and Bitchat app version. This decides which timing
   constants Slice 1 is validating.
2. Is there a public MeshCore repeater in range? It could replace buying a third
   radio for Slice 6.
3. Second bridge: second Cardputer kit (identical hardware) or Heltec V3 (cheaper)?

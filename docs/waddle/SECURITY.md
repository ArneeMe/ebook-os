# Security

What the review in November 2026 found, and what was done about it. The mesh
protocol itself is in protocol/PROTOCOL.md.

## Fixed

- **Anyone on the same Wi-Fi could take a T5's keys** while File Transfer was
  open: the unit showed the setup code but never waited for anyone to compare
  it. Now the code must be tapped on the T5's own screen before it opens or
  seals anything (`/api/mesh/join` and `/api/mesh/phone` answer 423 until then).
- **The hotspot password could be worked out** from the MAC address the hotspot
  sends. It is now random, new at every start.

## Watched, not fixed

Arne chose to see whether these happen before spending code on them. The Pi
writes them down (status page, "Mistenkelig"; `waddle mesh check`), at most
once per 10 minutes per kind and sender, kept 30 days. Nothing is blocked.

| Note | What it may be |
|---|---|
| ukjent avsender | a packet over LoRa from a number not in the roster |
| teller hopper | a counter far above the sender's newest: someone sending as that member to push its real SOS or position aside (the header is not signed) |
| ødelagt pakke | a broken WaddleOS packet over LoRa |
| feil token | a wrong token over HTTPS (someone guessing, if the port is open) |
| mye opplasting | more than 2000 packets in an hour from one member (a stolen phone or token) |

## Known and accepted

- Members share sender keys, so a member can forge a message, position or SOS
  to everyone as another member. DMs cannot be forged.
- ACKs (delivered, read, "has seen your SOS") are not signed.
- Firmware from arnee.me is not signed, and TLS to arnee.me is not checked.
- Keys sit unencrypted in the T5's flash: whoever has the unit has the keys.
  Take a lost unit out of the roster.
- The File Transfer web server has no login: anyone on the same Wi-Fi can read
  and change files while it is open.
- Radio headers are not encrypted: someone with a radio sees who writes to whom
  and when, not names or content.

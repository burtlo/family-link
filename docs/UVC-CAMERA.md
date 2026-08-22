# Child → parent photos (USB camera)

The BOX-3 has **no onboard camera**. Child → you snapshots need a **UVC/MJPEG USB 1.1** camera on the **dock USB-A** port. That is a later hardware demo (USB host + JPEG frames + POST `/v1/messages` `kind=image` as `box-a`).

Until a camera is on the desk:

- **You → child** photos are the path that matters for v1: parent page or `demos/parent/send_photo.py`, then firmware **h13** / product shell preview.
- Simulating a child snap from this Mac: POST an image **as `box-a`** (token `change-me-a`) so it lands in **box-b**’s inbox (the parent Mac tools). That proves the protocol, not the dock.

Do not block hangout or voicemail on UVC.

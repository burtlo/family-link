# Tryout: one box + your phone or computer

Yes. That is how v1 is supposed to work.

```
ESP32-S3-BOX-3  --Wi-Fi-->  small server  <--  iPhone / Mac / PC browser
   (desk / kid)                              (you)
```

A second box is only for a second child. You never needed a matching gadget on your side.

## What you can prove with one kit

| Flow | Works with 1 box + phone/Mac? |
|---|---|
| You send **text** → box shows it after PIN | Yes |
| You send a **small photo** from the iPhone camera roll → box displays it | Yes |
| You send a **voicemail** from the phone mic → box plays it | Yes |
| Box **hold-to-talk** → clip or live PTT on the phone | Yes |
| Live hangout (you tap Start on the phone, box PTT) | Yes — this is the Minecraft-night test, on your desk first |
| Child-originated **photo** | Not until a USB camera is on the dock |
| Two kids at once | Needs a second box (later) |

Put the box on **your** desk first. Talk to yourself: phone in one hand, box on the table. Same Wi-Fi is enough. When that feels right, the box goes to their house with the SSID already baked in; you keep using the same phone page.

## Clients

- **iPhone:** Safari (or a tiny installed web app). Mic + photos are native. This is the parent client you will actually carry.
- **Mac / PC:** same web page in Chrome or Safari. Better for flashing firmware (USB-C) and running the server in development.
- You can use **both**: Mac runs the server, iPhone is the client, box is the device.

iOS will prompt for microphone and photo access. That is expected.

## What you do not buy for a tryout

- A second BOX-3
- A kids LTE phone
- A webcam (unless you specifically want child → you photos on day one)
- The Waveshare / e-ink kit from the other repo

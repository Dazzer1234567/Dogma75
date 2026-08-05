# Antelope Control Panel → MIDI/OSC bridge — RE working doc

Goal: add MIDI/OSC control of mixer parameters (mutes first, maybe mix-send balance)
to an Antelope interface whose Control Panel exposes no MIDI/OSC. The mixer lives in
the device's FPGA DSP; the Control Panel (CP) is the only thing that speaks to it, over
USB with a proprietary protocol. This is a reverse-engineering-for-interoperability job.

**Scope / legal:** interop RE on hardware I own, permitted under EU Software Directive
2009/24/EC. Don't redistribute a decompiled protocol dump; if I ever publish the tool,
ship my own clean-room implementation, not their code.

---

## Fill these in before starting

- Device model: `________`
- OS I'll do the RE on: `________`  (Linux is by far the easiest — see Phase 0)
- CP variant: [ ] newer unified Control Panel  [ ] older per-device panel
- Target features (v1): [ ] mutes only  [ ] + mix-send levels  [ ] + monitor/dim/mono
- Do I need the CP running *live* during sessions (metering/FX)? [ ] yes [ ] no
  - If **no**, the whole "switching" problem collapses to "quit the CP." Confirm this early.

---

## Phase 0 — Recon (do this first, it de-risks everything)

The single most important step. The interface layout tells me which of the three attack
paths is actually open, and whether "switching" is even a problem.

### 0a. Enumerate USB interfaces

- Linux:   `lsusb`, then `lsusb -v -d <vid:pid>`
- Windows: USB Tree View (USB Device Tree Viewer)
- macOS:   System Information → USB, or IORegistryExplorer

Record, per interface:
- interface number (`MI_0x` on Windows composite devices)
- class (Audio / HID / Vendor-specific)
- endpoint types: **isochronous = audio stream**, **interrupt/bulk/control = the
  mixer control channel I care about**

Key question answered here: **is the control interface HID or vendor-specific?**
- **HID** → much easier. `hidapi` opens more permissively, often shareable, dodges the
  whole Windows driver-binding mess. May not need to "switch" at all — can inject
  alongside the CP.
- **Vendor-specific / exclusive** → `libusb`/WinUSB, and I'll be switching ownership
  rather than coexisting on that interface.

### 0b. Check for a local socket / Electron architecture

Some modern control apps split into background service + UI talking over localhost. If
this one does, that's the golden path — talk to the service directly, skip USB RE.

- Look in the CP install dir for `app.asar` → it's Electron → `npx asar extract app.asar out/`
  and the protocol is readable JS.
- With CP running, look for a localhost listener owned by the CP process:
  - Windows: `netstat -ano` + match PID in Task Manager
  - macOS/Linux: `lsof -i -P | grep -i antelope`

**Recon decision:**
- localhost socket / Electron found → **Path C** (talk to the socket; easiest by far)
- HID control interface → **Path A-HID** (coexist, inject alongside CP)
- vendor-specific interface, native CP → **Path B** (USB sniff + libusb sender)

---

## Phase 1 — Capture (Path B: USB sniffing)

Only needed if there's no socket to talk to. Do it on Linux if at all possible —
`usbmon` + interface separation is clean, and injecting later doesn't fight the audio
driver the way Windows/Zadig does.

### Capture tooling

- Linux:   `usbmon` + Wireshark (nicest)
- Windows: USBPcap + Wireshark
- macOS:   PCAP over USB, `XHC20` interface

### Critical: capture with the device IDLE

Do **not** stream 32ch while capturing.
- The mute command is a control/interrupt transfer the CP sends regardless of whether a
  DAW is streaming — so no audio is needed to see it.
- 32ch @ high SR over isoc turns the capture into a firehose (dropped packets, buried
  signal). No reason to eat that.
- If audio ever *is* running, filter it out losslessly:
  `usb.transfer_type != 0x00`  (0x00 = isochronous) — throws away the audio, keeps control.

Audio bandwidth for sanity: 32ch × 24-bit × 48k ≈ 4.6 Mbit/s each way — trivial vs
USB2's 480. Audio is never the bottleneck; it's only ever capture-volume noise, and I'm
sidestepping it by capturing idle.

---

## Phase 2 — Diff & decode

Method: drive one control to a known state, capture, toggle, capture, diff.

1. Mixer to known state, nothing else moving.
2. Toggle mute on **channel 1**, capture. Toggle back, capture.
3. Diff the transfers → mute is usually a single byte/bit flip; it stands out immediately
   against repeated toggles.
4. Repeat for **channel 2** → the index delta reveals the addressing. Now I can almost
   certainly **generalize** to all 32 channels without sniffing each (index arithmetic).

Per parameter, work out the `(target, param_id, value)` triple encoding:
- **Mute / solo / phantom / pad** — clean parameterized toggles, easy.
- **Gain / send level** — sniff 2–3 values to get the encoding. Watch for **nonlinear
  dB→raw curves**: gain is the one that's sometimes a lookup table, not linear. Capture
  a few points across the range before assuming a formula.

Build a small decode table / notes file as I go (`protocol-notes.md`), keyed by param.

---

## Phase 3 — Sender

Write the thing that emits those packets, driven by MIDI/OSC in.

- USB out: **PyUSB/libusb** (vendor iface) or **hidapi** (HID iface).
- Input layer: **python-osc** (OSC) and/or **mido** (MIDI). Map incoming
  messages → `(target, param_id, value)` → USB write.
- Start with fire-and-forget writes for the v1 feature set (mutes). No readback yet.

### Windows driver-binding caveat (Path B on Windows only)

The device is bound to Antelope's driver. To point WinUSB/libusb at **only** the control
interface of a composite device, bind via the per-interface `MI_0x` index. **Do not
rebind the audio interface** — Zadig on the wrong interface detaches the audio driver and
kills the 32ch. Be surgical: WinUSB on the control `MI_0x` only, audio stays on
Antelope's driver. This is the part most likely to bite. Linux/macOS hand you the vendor
interface while the audio class driver keeps streaming — no rebinding drama. **Prefer
Linux for this step.**

---

## Phase 4 — Ownership / "switching"

There is **no** "release control" command to send the CP. Ownership is arbitrated by the
OS/driver, not negotiated between apps. Two apps genuinely co-owning the *same* exclusive
vendor interface live doesn't work. So "switching" = arranging that only one app wants the
interface at a time.

Preference order:

1. **Different interfaces = no switch at all.** If control is HID (shareable) — or I'm on
   Linux claiming the vendor iface while the CP is closed — there's nothing to fight over.
   This is the happy case. Check in Phase 0.

2. **Quit the CP (robust default).** CP exits → releases its claim → my tool (running a
   claim-retry loop) grabs it. `libusb_claim_interface` returns `LIBUSB_ERROR_BUSY` while
   the other holds it, succeeds the instant it's free — so the handoff is automatic:
   whoever's running gets it. Want the CP back? Quit my tool, relaunch CP. Workflow
   becomes "CP for setup/FX, quit, custom surface for the session."

3. **Coexist by not contending.** If my tool only *sends* fire-and-forget commands and
   doesn't hold the interface open, per-command open→send→close may leave it free between
   messages — works only if the CP opens on demand rather than holding an exclusive claim
   the whole time. HID makes this genuinely viable (HID reports tolerate multiple openers).

4. **Force-detach — DON'T.** `libusb_detach_kernel_driver` can wrestle the interface away,
   but against the CP's userspace claim that's not the mechanism, and against the audio
   driver it kills the 32ch. Off the table.

---

## Architecture decision: coexist/layer vs full replacement

Decide up front — it defines the project size.

- **Coexist / layer (recommended).** Let the real CP do connect, init handshake, FX load,
  routing setup once. My tool injects only the live controls (mutes, mix balance) on top.
  Skips replicating the entire init sequence and the FX subsystem — which is the genuinely
  huge part (Synergy Core effect instantiation + coefficient uploads). This is where
  "only the features I need" pays off maximally.
- **Full replacement.** Only worth it if the feature set is small and self-contained and
  I'll replicate enough of the startup handshake (firmware query, capability negotiation,
  DSP/routing config) to bring the device up sane. Fine for a stripped mute/monitor box;
  painful if anything touches FX.

---

## State / readback (optional for v1)

Sending is trivial; *reading device state* is the harder, separate RE job (does the device
push notifications, or does the CP poll?). **Dodge for mutes:** make my tool the sole
authority for the params it owns and track state locally — I send mute-on, I know it's
muted, I never ask the device. Works perfectly as long as nothing else fights me for those
params. Only invest in readback if I want my surface to reflect changes made elsewhere
(front panel / CP running alongside).

---

## Known caveat: firmware coupling

The protocol can shift across firmware updates. My clone is pinned to whatever firmware I
REd against. **Pin the firmware**, or budget for occasional re-sniffing after updates.

---

## References

- **scarlett2** (Linux kernel/ALSA): fully RE'd control protocol for Focusrite
  Scarlett/Clarett mixers — mutes, gains, routing — built exactly this way (sniff, diff,
  generalize) and upstreamed. Best reference for structuring command decode + state model.
- **RME TotalMix OSC**: reference for what a good OSC control surface for a DSP mixer looks
  like (official interface, not RE'd, but good design prior).

---

## Tooling checklist

- [ ] `lsusb` / USB Tree View / IORegistryExplorer (Phase 0 enumerate)
- [ ] Wireshark + USBPcap (Win) / usbmon (Linux) (Phase 1 capture)
- [ ] `asar` (if CP is Electron)
- [ ] `lsof` / `netstat` (socket check)
- [ ] Python: `pyusb` or `hidapi`, `python-osc`, `mido` + a virtual MIDI port
- [ ] (Windows only) Zadig — used surgically on the control `MI_0x` interface ONLY

## First session, concretely

1. Phase 0 enumerate → is control HID or vendor? Is there an `app.asar` / localhost socket?
2. That single answer picks Path A-HID / B / C and tells me if switching is even a problem.
3. If Path B: idle capture, toggle ch1/ch2 mute, diff, confirm I can generalize the index.
4. Minimal sender: one OSC message → mute ch N. Everything else builds from there.

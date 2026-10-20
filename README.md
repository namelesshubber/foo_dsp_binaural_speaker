# foo_dsp_cmss3d - Speaker Virtualizer (CMSS-3D Virtual style)

A foobar2000 (2.x) DSP component that plays **binaural stereo** (material normally meant for
headphones) over **two ordinary stereo loudspeakers** by cancelling acoustic crosstalk.

It is an open re-implementation of the *idea* behind Creative's "CMSS-3D Virtual" for 2/2.1 speakers.
It is **not** a bit-exact clone: Creative's actual algorithm and filter data are proprietary and unpublished
(see "What is publicly known" below).

## Intended signal chain

```
decoder (Dolby Surround / discrete multichannel)
   -> Dolby Headphone wrapper        (binaural stereo out: HRTF + room already applied)
   -> foo_dsp_cmss3d  (this DSP)     (crosstalk cancellation for speakers)
   -> stereo speakers
```

Because Dolby Headphone already performs the binaural (HRTF) synthesis, this DSP only needs to do the
second half of a "virtual surround" system: the **crosstalk canceller** (a.k.a. transaural stage).
Put it **after** the Dolby Headphone wrapper in the DSP chain. Do not use it together with headphones.
It only processes 2-channel audio; other layouts pass through untouched.

## How it works

For a listener centred between speakers at +/- phi, the speaker-to-ear paths form a symmetric matrix:

```
ears = [ Hi Hc ] * speakers        Hi = same-side path, Hc = opposite-side path
       [ Hc Hi ]
```

The Mid/Side transform diagonalises it, so the inverse is two filters:

* Mid  `(L+R)/sqrt2` is filtered by `1 / (Hi + Hc)`
* Side `(L-R)/sqrt2` is filtered by `1 / (Hi - Hc)`

`Hi` and `Hc` come from a head model (no HRTF database needed). Two models are available:

* **Exact rigid sphere (default since v0.2):** the Rayleigh partial-wave series for a plane wave on a
  rigid sphere (Aaronson & Hartmann 2014, Eq. 2), evaluated at the two ear positions (ear axis at 100 deg).
  Since v0.4 the speakers are treated as point sources at their real distance (Duda & Martens 1998), so the
  level and phase difference between the ears at low frequencies follows the distance you enter. At 200 Hz the
  contralateral/ipsilateral level ratio is about -0.7 dB at 1 m and -1.7 dB at 40 cm, compared with 0 dB for a
  plane wave; that matters because the cancellation depends on small differences there.
* **Classic:** Woodworth interaural time difference plus the Brown & Duda head-shadow filter.
  Compared with the exact sphere at a 15 deg speaker half-angle, its interaural delay is within about
  25 us up to 3 kHz and its level difference within about 2 dB, so it is a good approximation, but the
  mismatch costs cancellation depth when the real head behaves like the sphere.
 The inversion is Tikhonov-regularised,
boost-limited and limited to a frequency band: it fades out above a configurable upper frequency
(cancellation is only reliable at low and mid frequencies) and fades back to plain stereo below a
configurable lower frequency (at very long wavelengths the two speakers add up in the room and the
crosstalk cannot be cancelled, so the filter would only change the bass level without any benefit). The resulting filters are minimum-phase FIRs (zero added latency,
about 10 ms of taps, 256-1024 taps depending on sample rate) designed with an FFT/cepstrum method.

`tests/test_core.cpp` checks the maths offline: with weak regularisation the simulated channel
separation at the ears is 20-55 dB between 400 Hz and 7 kHz.

## Settings

| Setting | Meaning |
|---|---|
| Speaker spacing | Distance between the two speakers (centre to centre of the drivers), in cm. |
| Listening distance | Distance from your head to the line joining the two speakers, in cm. Spacing and distance give the speaker half-angle (shown in the dialog) and the head-to-speaker distance. Typical desktop: 50 cm spacing at 60-70 cm (about 20-25 deg half-angle). Living room: 200 cm at 200 cm (45 deg). A narrower angle gives a stronger effect but more bass/colour risk. |
| Head radius | Head size for the model (default 87.5 mm). |
| Exact rigid-sphere model | On (default): exact diffraction series. Off: classic Woodworth + Brown/Duda model, as in v0.1. Presets saved with v0.1 keep the classic model. |
| Regularization | Higher = safer, less coloured, weaker cancellation. Lower = stronger effect, more colouration. |
| Max filter boost | Hard cap on the gain of each canceller filter. |
| Canceller lower limit | Below this frequency (default 150 Hz, one octave wide transition) the filters fade back to unity, so the bass is the same as with the DSP switched off. 0 = off (cancel down to DC, as in v0.1/v0.2). |
| Canceller upper limit | Cancellation fades out around this frequency (one octave wide transition). |
| Output gain / Auto level | Level compensation. Auto level normalises the average 200 Hz-8 kHz power gain to 0 dB. |

The effect works only near the sweet spot (centred, ears level with the speakers, speakers
toed in towards you) and in a room with limited early reflections. Leave some headroom
(e.g. ReplayGain or a limiter after this DSP) as the filters can raise peaks.

## Installing

* Double-click `foo_dsp_cmss3d.fb2k-component`, or in foobar2000 go to
  *Preferences -> Components -> Install...*, then restart foobar2000.
* Manual install (portable or classic): put `foo_dsp_cmss3d.dll` into
  `<profile>\user-components\foo_dsp_cmss3d\` for 32-bit foobar2000, or the 64-bit DLL
  into `<profile>\user-components-x64\foo_dsp_cmss3d\` for 64-bit foobar2000.
* Preferences -> Playback -> DSP Manager: add **Speaker Virtualizer (CMSS-3D Virtual style)**
  *after* your Dolby Headphone wrapper, then use the configure button.

The DLL uses the dynamic C runtime (/MD), like the foobar2000 SDK libraries, and a Windows 7 target level.
foobar2000 provides the Visual C++ runtime to its components.

## What is publicly known about CMSS-3D Virtual

Public sources only describe it in general terms:

* CMSS-3D Virtual (also called Stereo Xpand) is used when the Creative panel is set to 2/2.1 or
  4/4.1 speakers; it creates virtual 3D positional audio like CMSS-3D Headphone but adds
  **crosstalk cancellation**, requires the listener to sit in the sweet spot, and is not
  available for 5.1 or more speakers (Wikipedia, "Sound Blaster X-Fi").
* Forum analyses describe it as HRTF filtering combined with crosstalk cancellation.
* No source code, filter coefficients or technical specification have been published.

The general technique is textbook: Atal & Schroeder (1966) crosstalk cancellation, Bauck & Cooper (1996)
generalised transaural stereo, Brown & Duda (1998) structural head model, Aaronson & Hartmann (2014,
J. Acoust. Soc. Am. 135, 817-823) for the Woodworth/rigid-sphere analysis, and many patents describing
HRTF + crosstalk-cancellation virtualizers (for example US 6,009,178, US 8,155,323, US 7,974,418, US 8,050,433).
This project implements that general technique with its own code. It is provided for personal use
and is not legal advice regarding any patent.

## Changelog

* **0.4** - Speaker spacing and listening distance replace the half-angle control; the exact sphere model now uses
  point sources at the real distance. Presets from 0.1-0.3 load with the angle converted to a spacing at a 1 m distance.
* **0.3** - Canceller lower limit (fades back to plain stereo at low frequencies). Band limits are now
  blended on magnitudes, which avoids notches in the transition regions. Presets from 0.1/0.2 load
  with the lower limit off, so they sound as before.
* **0.2** - Exact rigid-sphere head model (default) with a switch back to the classic model.
* **0.1** - First release (Woodworth + Brown/Duda head model).

## License

MIT for the code in this repository (see `LICENSE`). The foobar2000 SDK is downloaded separately
under its own license.

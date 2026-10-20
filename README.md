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
* **Classic:** Woodworth interaural time difference plus the Brown & Duda head-shadow filter.
  Compared with the exact sphere at a 15 deg speaker half-angle, its interaural delay is within about
  25 us up to 3 kHz and its level difference within about 2 dB, so it is a good approximation, but the
  mismatch costs cancellation depth when the real head behaves like the sphere.
 The inversion is Tikhonov-regularised, boost-limited and fades out above a configurable frequency,
because cancellation is only reliable at low and mid frequencies.
The resulting filters are minimum-phase FIRs (zero added latency, about 10 ms of taps,
256-1024 taps depending on sample rate) designed with an FFT/cepstrum method.

`tests/test_core.cpp` checks the maths offline: with weak regularisation the simulated channel
separation at the ears is 20-55 dB between 400 Hz and 7 kHz.

## Settings

| Setting | Meaning |
|---|---|
| Speaker half-angle | Half of the angle between your speakers as seen from your seat. Desktop speakers: 8-15 deg. Living room: 25-30 deg. Narrower = stronger effect but more bass/colour risk. |
| Head radius | Head size for the model (default 87.5 mm). |
| Exact rigid-sphere model | On (default): exact diffraction series. Off: classic Woodworth + Brown/Duda model, as in v0.1. Presets saved with v0.1 keep the classic model. |
| Regularization | Higher = safer, less coloured, weaker cancellation. Lower = stronger effect, more colouration. |
| Max filter boost | Hard cap on the gain of each canceller filter. |
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

**Wrapping the original CMSS-3D is not realistic:** it lives inside Creative's drivers/DSP
hardware and is not exposed as a redistributable library, so there is nothing to wrap.

## License

MIT for the code in this repository (see `LICENSE`). The foobar2000 SDK is downloaded separately
under its own license.

// main.cpp - foobar2000 DSP component "Speaker Virtualizer (CMSS-3D Virtual style)".
//
// Signal chain it is designed for:
//   decoder -> (Dolby Headphone wrapper, binaural stereo out) -> THIS DSP -> stereo speakers
//
// The DSP only processes 2-channel chunks; anything else is passed through untouched.
#include <foobar2000/SDK/foobar2000.h>

#include <commctrl.h>
#include <cstdio>
#include <cwchar>

#include "ctc_core.h"
#include "resource.h"
#include "settings.h"

DECLARE_COMPONENT_VERSION(
    "Speaker Virtualizer (CMSS-3D Virtual style)",
    "0.2.0",
    "Crosstalk canceller (transaural stage) for playing binaural stereo\n"
    "material, such as Dolby Headphone output, over two loudspeakers.\n\n"
    "Uses a rigid-sphere head model (exact diffraction series, or the classic\n"
    "Woodworth + Brown/Duda model) and a regularised Mid/Side inverse. Not affiliated with Creative.\n\n"
    "Source: see README.md in the project repository.");

VALIDATE_COMPONENT_FILENAME("foo_dsp_cmss3d.dll");

namespace {

// {a346bf79-afa2-4c39-9069-82cc2fe200eb}
const GUID g_guid_dsp = {0xa346bf79, 0xafa2, 0x4c39, {0x90, 0x69, 0x82, 0xcc, 0x2f, 0xe2, 0x00, 0xeb}};

void paramsToPreset(const ctc::Params& p, dsp_preset& out) {
    settings::Blob blob;
    settings::toBlob(p, blob);
    out.set_owner(g_guid_dsp);
    out.set_data(&blob, sizeof(blob));
}

void presetToParams(const dsp_preset& in, ctc::Params& p) {
    p = ctc::Params();
    settings::fromBlob(in.get_data(), in.get_data_size(), p);
}

// ---------------------------------------------------------------------------
// DSP
// ---------------------------------------------------------------------------
class dsp_cmss3d : public dsp_impl_base {
public:
    explicit dsp_cmss3d(const dsp_preset& in) : m_rate(0) { presetToParams(in, m_params); }

    static GUID g_get_guid() { return g_guid_dsp; }

    static void g_get_name(pfc::string_base& p_out) { p_out = "Speaker Virtualizer (CMSS-3D Virtual style)"; }

    bool on_chunk(audio_chunk* chunk, abort_callback&) {
        if (chunk->get_channels() != 2) return true;  // not stereo: leave untouched
        const unsigned rate = chunk->get_srate();
        if (rate != m_rate || !m_engine.configured()) {
            m_rate = rate;
            m_engine.configure(m_params, static_cast<double>(rate));
        }
        m_engine.process(chunk->get_data(), chunk->get_sample_count());
        return true;
    }

    void on_endofplayback(abort_callback&) { m_engine.reset(); }
    void on_endoftrack(abort_callback&) {}  // keep filter state: gapless friendly
    void flush() { m_engine.reset(); }
    double get_latency() { return 0.0; }    // minimum-phase filters, no added delay
    bool need_track_change_mark() { return false; }

    static bool g_get_default_preset(dsp_preset& p_out) {
        paramsToPreset(ctc::Params(), p_out);
        return true;
    }

    static bool g_have_config_popup() { return true; }
    static void g_show_config_popup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback);

private:
    ctc::Params m_params;
    ctc::Engine m_engine;
    unsigned m_rate;
};

// ---------------------------------------------------------------------------
// Configuration dialog (plain Win32, resource in config.rc)
// ---------------------------------------------------------------------------
struct DlgState {
    ctc::Params params;
};

void setSlider(HWND dlg, int id, int minPos, int maxPos, int pos) {
    SendDlgItemMessage(dlg, id, TBM_SETRANGE, TRUE, MAKELONG(minPos, maxPos));
    SendDlgItemMessage(dlg, id, TBM_SETPOS, TRUE, pos);
}

int getSlider(HWND dlg, int id) { return static_cast<int>(SendDlgItemMessage(dlg, id, TBM_GETPOS, 0, 0)); }

void writeControls(HWND dlg, const ctc::Params& p) {
    setSlider(dlg, IDC_SL_ANGLE, 6, 120, static_cast<int>(std::lround(p.speakerHalfAngleDeg * 2.0)));
    setSlider(dlg, IDC_SL_HEAD, 70, 100, static_cast<int>(std::lround(p.headRadiusMm)));
    setSlider(dlg, IDC_SL_REG, 0, 300, static_cast<int>(std::lround((std::log10(p.regularization) + 3.0) * 100.0)));
    setSlider(dlg, IDC_SL_BOOST, 0, 20, static_cast<int>(std::lround(p.maxBoostDb)));
    setSlider(dlg, IDC_SL_FREQ, 20, 140, static_cast<int>(std::lround(p.cancelMaxHz / 100.0)));
    setSlider(dlg, IDC_SL_GAIN, 0, 36, static_cast<int>(std::lround((p.outputGainDb + 12.0) * 2.0)));
    CheckDlgButton(dlg, IDC_CHK_AUTO, p.autoLevel ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dlg, IDC_CHK_EXACT, p.exactSphere ? BST_CHECKED : BST_UNCHECKED);
}

void readControls(HWND dlg, ctc::Params& p) {
    p.speakerHalfAngleDeg = getSlider(dlg, IDC_SL_ANGLE) / 2.0;
    p.headRadiusMm = getSlider(dlg, IDC_SL_HEAD);
    p.regularization = std::pow(10.0, -3.0 + getSlider(dlg, IDC_SL_REG) / 100.0);
    p.maxBoostDb = getSlider(dlg, IDC_SL_BOOST);
    p.cancelMaxHz = getSlider(dlg, IDC_SL_FREQ) * 100.0;
    p.outputGainDb = getSlider(dlg, IDC_SL_GAIN) / 2.0 - 12.0;
    p.autoLevel = IsDlgButtonChecked(dlg, IDC_CHK_AUTO) == BST_CHECKED;
    p.exactSphere = IsDlgButtonChecked(dlg, IDC_CHK_EXACT) == BST_CHECKED;
    ctc::sanitize(p);
}

void updateLabels(HWND dlg, const ctc::Params& p) {
    wchar_t buf[64];
    swprintf_s(buf, L"%.1f deg", p.speakerHalfAngleDeg);
    SetDlgItemTextW(dlg, IDC_TX_ANGLE, buf);
    swprintf_s(buf, L"%.0f mm", p.headRadiusMm);
    SetDlgItemTextW(dlg, IDC_TX_HEAD, buf);
    swprintf_s(buf, L"%.3f", p.regularization);
    SetDlgItemTextW(dlg, IDC_TX_REG, buf);
    swprintf_s(buf, L"%.0f dB", p.maxBoostDb);
    SetDlgItemTextW(dlg, IDC_TX_BOOST, buf);
    swprintf_s(buf, L"%.1f kHz", p.cancelMaxHz / 1000.0);
    SetDlgItemTextW(dlg, IDC_TX_FREQ, buf);
    swprintf_s(buf, L"%+.1f dB", p.outputGainDb);
    SetDlgItemTextW(dlg, IDC_TX_GAIN, buf);
}

INT_PTR CALLBACK configProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        DlgState* st = reinterpret_cast<DlgState*>(lp);
        SetWindowLongPtr(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        writeControls(dlg, st->params);
        readControls(dlg, st->params);  // normalise to slider resolution
        updateLabels(dlg, st->params);
        return TRUE;
    }
    case WM_HSCROLL: {
        DlgState* st = reinterpret_cast<DlgState*>(GetWindowLongPtr(dlg, GWLP_USERDATA));
        if (st) {
            readControls(dlg, st->params);
            updateLabels(dlg, st->params);
        }
        return TRUE;
    }
    case WM_COMMAND: {
        DlgState* st = reinterpret_cast<DlgState*>(GetWindowLongPtr(dlg, GWLP_USERDATA));
        switch (LOWORD(wp)) {
        case IDC_CHK_AUTO:
        case IDC_CHK_EXACT:
            if (st) readControls(dlg, st->params);
            return TRUE;
        case IDC_BTN_RESET:
            if (st) {
                st->params = ctc::Params();
                writeControls(dlg, st->params);
                readControls(dlg, st->params);
                updateLabels(dlg, st->params);
            }
            return TRUE;
        case IDOK:
            if (st) readControls(dlg, st->params);
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

void dsp_cmss3d::g_show_config_popup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback) {
    DlgState st;
    presetToParams(p_data, st.params);

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    modal_dialog_scope scope;
    if (!scope.can_create()) return;
    scope.initialize(p_parent);

    const INT_PTR r = DialogBoxParam(core_api::get_my_instance(), MAKEINTRESOURCE(IDD_CONFIG), p_parent,
                                     configProc, reinterpret_cast<LPARAM>(&st));
    if (r == IDOK) {
        dsp_preset_impl preset;
        paramsToPreset(st.params, preset);
        p_callback.on_preset_changed(preset);
    }
}

dsp_factory_t<dsp_cmss3d> g_dsp_cmss3d_factory;

} // namespace

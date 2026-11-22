#include "calib_dlg.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include <algorithm>
#include <sstream>
#include <iterator>
#include "slic3r/GUI/Widgets/CheckBox.hpp"
#include <wx/anybutton.h>
#include <wx/checklst.h>
#include "libslic3r/Config.hpp"
#include <cstddef>
#include "slic3r/GUI/GUI_Utils.hpp"
#include <wx/colour.h>
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include <slic3r/GUI/Widgets/LabeledCheckBox.hpp>
#include "slic3r/GUI/Widgets/LabeledStaticBox.hpp"
#include "slic3r/GUI/Widgets/RadioGroup.hpp"
#include "slic3r/GUI/Widgets/TextInput.hpp"
#include "libslic3r/calib.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/Widgets/ComboBox.hpp"
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include "slic3r/GUI/wxExtensions.hpp"
#include <wx/dcgraph.h>
#include <wx/dcbuffer.h>
#include "MainFrame.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Preset.hpp"
#include "format.hpp"
#include "PresetComboBoxes.hpp"
#include "Tab.hpp"
#include "Widgets/DialogButtons.hpp"
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <wx/string.h>
#include <wx/gdicmn.h>
#include <wx/sizer.h>
#include <wx/dialog.h>
#include <wx/valtext.h>
#include <wx/tglbtn.h>
#include <wx/utils.h>
#include <wx/event.h>
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Utils.hpp"
#include "Plater.hpp"

namespace Slic3r { namespace GUI {

std::vector<InputShaperType> input_shaper_types_for_flavor(GCodeFlavor flavor);

namespace {

void ParseStringValues(std::string str, std::vector<double> &vec)
{
    vec.clear();
    std::replace(str.begin(), str.end(), ',', ' ');
    std::istringstream inss(str);
    std::copy_if(std::istream_iterator<int>(inss), std::istream_iterator<int>(), std::back_inserter(vec),
                 [](int x){ return x > 0; });
}

int GetTextMax(wxWindow* parent, const std::vector<wxString>& labels)
{
    wxSize text_size;
    for (wxString label : labels)
        text_size.IncTo(parent->GetTextExtent(label));
    return text_size.x + parent->FromDIP(10);
}

LabeledCheckBox* add_scale_checkbox(wxWindow* parent, wxSizer* settings_sizer)
{
    auto cb   = new LabeledCheckBox(parent, _L("Auto-scale for nozzle"));
    cb->SetValue(true);
    cb->SetToolTip(_L("This model is designed around a 0.4 mm nozzle with a 0.2 mm layer height.\n"
                      "When the scaling option is enabled (recommended), it dynamically resizes to match your current nozzle diameter"
                      " and an appropriate layer height, making the test both accurate and easy to read.\n"
                      "Turn scaling off only if you wish to print the reference model exactly as-is."));
    settings_sizer->Add(cb, 0, wxLEFT | wxTOP, parent->FromDIP(3));
    return cb;
}

static wxPanel* create_filament_color_swatch(wxWindow* parent, const std::string& color)
{
    wxColour filament_color = wxColour(color);
    if (!filament_color.IsOk())
        filament_color = wxColour("WHITE");
    const int swatch_size = parent->FromDIP(20);
    auto* swatch = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(swatch_size, swatch_size));
    swatch->SetBackgroundStyle(wxBG_STYLE_PAINT);
    swatch->SetMinSize(wxSize(swatch_size, swatch_size));
    swatch->Bind(wxEVT_PAINT, [swatch, filament_color](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(swatch);
        const wxSize size = swatch->GetClientSize();
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(filament_color));
        dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

        const bool dark_mode = wxGetApp().dark_mode();
        const bool near_white = filament_color.Red() > 224 && filament_color.Green() > 224 && filament_color.Blue() > 224;
        const bool near_black = filament_color.Red() < 45 && filament_color.Green() < 45 && filament_color.Blue() < 45;
        if ((!dark_mode && near_white) || (dark_mode && near_black)) {
            dc.SetPen(wxPen(dark_mode ? wxColour(207, 207, 207) : wxColour(130, 130, 128), 1));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());
        }
    });
    return swatch;
}

std::vector<std::string> get_shaper_type_values()
{
    if (auto* preset_bundle = wxGetApp().preset_bundle) {
        auto printer_config = &preset_bundle->printers.get_edited_preset().config;
        const auto* gcode_flavor_option = printer_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor");
        const ConfigOptionDef* def = printer_config->def()->get("input_shaping_type");
        if (gcode_flavor_option) {
            auto types = input_shaper_types_for_flavor(gcode_flavor_option->value);
            if (!types.empty()) {
                std::vector<std::string> values;
                values.reserve(types.size());
                for (InputShaperType type : types) {
                    if (type == InputShaperType::Disable)
                        continue;
                    const size_t idx = static_cast<size_t>(type);
                    if (def && idx < def->enum_values.size())
                        values.push_back(def->enum_values[idx]);
                    else
                        values.push_back(std::to_string(static_cast<int>(type)));
                }
                if (!values.empty())
                    return values;
            }
        }

        if (def && !def->enum_values.empty())
            return {def->enum_values.front()};
    }
    return {"Default"};
}

std::vector<wxString> make_shaper_type_labels()
{
    auto values = get_shaper_type_values();
    if (values.empty())
        values.emplace_back("");

    std::vector<wxString> labels;
    labels.reserve(values.size());
    for (const auto& label : values)
        labels.emplace_back(wxString::FromUTF8(label.c_str()));
    return labels;
}

// ORCA-Belt: PA Line / PA Pattern have belt plumbing in place (drawn on the
// belt surface via BeltKinematics world-coordinates mode) but are not
// validated yet — belt printers are restricted to the PA Tower for now.
bool is_belt_printer_selected()
{
    if (auto* preset_bundle = wxGetApp().preset_bundle) {
        const auto& cfg = preset_bundle->printers.get_edited_preset().config;
        const auto* opt = cfg.option<ConfigOptionBool>("belt_printer");
        return opt != nullptr && opt->value;
    }
    return false;
}

// The rule the slicer uses to find a filament's extruder; -1 while an automatic filament map is unresolved.
int calib_filament_extruder(size_t filament_id)
{
    PresetBundle&       bundle  = *wxGetApp().preset_bundle;
    const Plater&       plater  = *wxGetApp().plater();
    const auto*         nozzles = bundle.printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
    return bed_exclusion_extruder_for_filament(filament_id, plater.get_global_filament_map(), plater.get_global_filament_map_mode(),
                                               bundle.is_bbl_vendor(), false, nozzles != nullptr ? nozzles->size() : 1);
}

}

CalibFilamentSelector::CalibFilamentSelector(wxWindow* parent) : m_parent(parent)
{
    m_sizer     = new wxBoxSizer(wxHORIZONTAL);
    auto* label = new wxStaticText(parent, wxID_ANY, _L("Filament: "));
    m_combo     = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, parent->FromDIP(wxSize(260, -1)), 0, nullptr,
                                 wxCB_READONLY);
    m_sizer->Add(label, 0, wxALL | wxALIGN_CENTER_VERTICAL, parent->FromDIP(2));
    m_sizer->Add(m_combo, 1, wxALL | wxALIGN_CENTER_VERTICAL, parent->FromDIP(2));
    refresh();
    m_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& event) {
        if (m_on_change)
            m_on_change();
        event.Skip();
    });
    // The dialogs are reused, so the list follows the project's filaments each time one opens.
    parent->Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
        if (event.IsShown()) {
            refresh();
            if (m_on_change)
                m_on_change();
            m_parent->GetSizer()->SetSizeHints(m_parent);
        }
        event.Skip();
    });
}

void CalibFilamentSelector::refresh()
{
    const PresetBundle& bundle = *wxGetApp().preset_bundle;
    const size_t count = std::min(bundle.num_physical_filaments(), bundle.filament_presets.size());
    const auto*  nozzles = bundle.printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
    const bool   multi_extruder = nozzles != nullptr && nozzles->size() > 1;

    int selection = m_combo->GetSelection();
    if (selection < 0 || size_t(selection) >= count) {
        // Start on the filament the filament tab has open.
        Tab* tab  = wxGetApp().get_tab(Preset::TYPE_FILAMENT);
        selection = tab != nullptr ? tab->get_combo_box()->get_filament_idx() : 0;
    }

    m_combo->Clear();
    const std::vector<wxBitmap*> icons = get_extruder_color_icons(true);
    for (size_t i = 0; i < count; ++i) {
        const Preset* preset = bundle.filaments.find_preset(bundle.filament_presets[i]);
        wxString      name   = wxString::FromUTF8(preset != nullptr ? preset->label(false) : bundle.filament_presets[i]);
        if (const int extruder = calib_filament_extruder(i); multi_extruder && extruder >= 0)
            // TRN %1% is a filament preset name, %2% the number of the extruder that prints it.
            name = format_wxstr(_L("%1% (Extruder %2%)"), name, extruder + 1);
        m_combo->Append(name, i < icons.size() && icons[i] != nullptr ? *icons[i] : wxNullBitmap);
    }
    if (count > 0)
        m_combo->SetSelection(std::clamp(selection, 0, int(count) - 1));
    m_sizer->ShowItems(count > 1);
}

int CalibFilamentSelector::filament_id() const { return std::max(m_combo->GetSelection(), 0); }

int CalibFilamentSelector::extruder_id() const { return std::max(calib_filament_extruder(size_t(filament_id())), 0); }

void CalibFilamentSelector::apply_to(Calib_Params& params) const
{
    params.filament_id = filament_id();
    params.extruder_id = extruder_id();
}

const Preset* CalibFilamentSelector::preset() const
{
    // find_preset() returns the edited copy of the preset the filament tab has open.
    const PresetBundle& bundle = *wxGetApp().preset_bundle;
    const size_t        id     = size_t(filament_id());
    return id < bundle.filament_presets.size() ? bundle.filaments.find_preset(bundle.filament_presets[id], true) : nullptr;
}

PA_Calibration_Dlg::PA_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("PA Calibration"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);
    m_filament = std::make_unique<CalibFilamentSelector>(this);
    v_sizer->Add(m_filament->sizer(), 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Extruder type Radio Group
    auto labeled_box_type = new LabeledStaticBox(this, _L("Extruder type"));
    auto type_box = new wxStaticBoxSizer(labeled_box_type, wxHORIZONTAL);

    m_rbExtruderType = new RadioGroup(this, {_L("DDE"), _L("Bowden")}, wxHORIZONTAL);
    type_box->Add(m_rbExtruderType, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(type_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Method Radio Group
    auto labeled_box_method = new LabeledStaticBox(this, _L("Method"));
    auto method_box = new wxStaticBoxSizer(labeled_box_method, wxHORIZONTAL);

	m_rbMethod = new RadioGroup(this, { _L("PA Tower"), _L("PA Line"), _L("PA Pattern") }, wxHORIZONTAL);
    method_box->Add(m_rbMethod, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(method_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString start_pa_str    = _L("Start PA: ");
    wxString end_pa_str      = _L("End PA: ");
    wxString PA_step_str     = _L("PA step: ");
    wxString sp_accel_str    = _L("Accelerations: ");
    wxString sp_speed_str    = _L("Speeds: ");
    wxString cb_print_no_str = _L("Print numbers");

    int text_max = GetTextMax(this, std::vector<wxString>{start_pa_str, end_pa_str, PA_step_str, sp_accel_str, sp_speed_str, cb_print_no_str});

    auto st_size = wxSize(text_max, -1);
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // start PA
    auto start_PA_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_pa_text = new wxStaticText(this, wxID_ANY, start_pa_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStartPA = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_PROCESS_ENTER);
    m_tiStartPA->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
	start_PA_sizer->Add(start_pa_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_PA_sizer->Add(m_tiStartPA  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_PA_sizer, 0, wxLEFT, FromDIP(3));

    // end PA
    auto end_PA_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_pa_text = new wxStaticText(this, wxID_ANY, end_pa_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiEndPA = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_PROCESS_ENTER);
    m_tiEndPA->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_PA_sizer->Add(end_pa_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    end_PA_sizer->Add(m_tiEndPA  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_PA_sizer, 0, wxLEFT, FromDIP(3));

    // PA step
    auto PA_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto PA_step_text = new wxStaticText(this, wxID_ANY, PA_step_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiPAStep = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_PROCESS_ENTER);
    m_tiPAStep->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    PA_step_sizer->Add(PA_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    PA_step_sizer->Add(m_tiPAStep  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(PA_step_sizer, 0, wxLEFT, FromDIP(3));

    // Print Numbers
    wxBoxSizer* cb_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto cb_title = new wxStaticText(this, wxID_ANY, cb_print_no_str, wxDefaultPosition, st_size, 0);
    m_cbPrintNum = new ::CheckBox(this);
    m_cbPrintNum->SetValue(false);
    m_cbPrintNum->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) {
        (m_params.print_numbers) = (m_params.print_numbers) ? false : true;
        e.Skip();
    });
    cb_sizer->Add(cb_title      , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    cb_sizer->Add(m_cbPrintNum  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(cb_sizer, 0, wxLEFT | wxTOP | wxBOTTOM, FromDIP(3));

    wxTextValidator val_list_validator(wxFILTER_INCLUDE_CHAR_LIST);
    val_list_validator.SetCharIncludes(wxString("0123456789,"));

    auto sp_accel_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto sp_accel_text = new wxStaticText(this, wxID_ANY, sp_accel_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiBMAccels = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_PROCESS_ENTER);
    m_tiBMAccels->SetToolTip(_L("Comma-separated list of printing accelerations"));
    m_tiBMAccels->GetTextCtrl()->SetValidator(val_list_validator);
    sp_accel_sizer->Add(sp_accel_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    sp_accel_sizer->Add(m_tiBMAccels , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(sp_accel_sizer, 0, wxLEFT, FromDIP(3));

    auto sp_speed_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto sp_speed_text = new wxStaticText(this, wxID_ANY, sp_speed_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiBMSpeeds = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_PROCESS_ENTER);
    m_tiBMSpeeds->SetToolTip(_L("Comma-separated list of printing speeds"));
    m_tiBMSpeeds->GetTextCtrl()->SetValidator(val_list_validator);
    sp_speed_sizer->Add(sp_speed_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    sp_speed_sizer->Add(m_tiBMSpeeds , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(sp_speed_sizer, 0, wxLEFT, FromDIP(3));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});

    auto bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto wiki_btn = new Button(this, "", "toolbar_wiki", 0, 15);
    auto wiki_url = "https://www.orcaslicer.com/wiki/pressure_advance_calib";
    wiki_btn->SetToolTip(_L("Wiki Guide") + "\n" + wiki_url);
    wiki_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Circle);
    wiki_btn->SetCanFocus(false);
    wiki_btn->Bind(wxEVT_LEFT_DOWN, ([wiki_url](auto& e) {wxLaunchDefaultBrowser(wiki_url);}));

    bottom_sizer->Add(wiki_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    bottom_sizer->AddStretchSpacer();
    bottom_sizer->Add(dlg_btns, 0, wxEXPAND);
    v_sizer->Add(bottom_sizer, 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &PA_Calibration_Dlg::on_start, this);

    PA_Calibration_Dlg::reset_params();

    // Connect Events
    m_rbExtruderType->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_extruder_type_changed), NULL, this);
    m_rbMethod->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_method_changed), NULL, this);
    this->Connect(wxEVT_SHOW, wxShowEventHandler(PA_Calibration_Dlg::on_show));
    
    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
    v_sizer->SetSizeHints(this);
}

PA_Calibration_Dlg::~PA_Calibration_Dlg() {
    // Disconnect Events
    m_rbExtruderType->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_extruder_type_changed), NULL, this);
    m_rbMethod->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_method_changed), NULL, this);
}

void PA_Calibration_Dlg::reset_params() {
    bool isDDE = m_rbExtruderType->GetSelection() == 0 ? true : false;
    int method = m_rbMethod->GetSelection();

    m_tiStartPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.0));

    switch (method) {
        case 1:
            m_params.mode = CalibMode::Calib_PA_Line;
            m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.1));
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.002));
            m_cbPrintNum->SetValue(true);
            m_cbPrintNum->Enable(true);
            m_tiBMAccels->Enable(false);
            m_tiBMSpeeds->Enable(false);
            break;
        case 2:
            m_params.mode = CalibMode::Calib_PA_Pattern;
            m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.08));
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.005));
            m_cbPrintNum->SetValue(true);
            m_cbPrintNum->Enable(false);
            m_tiBMAccels->Enable(true);
            m_tiBMSpeeds->Enable(true);
            break;
        default:
            m_params.mode = CalibMode::Calib_PA_Tower;
            m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.1));
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.002));
            m_cbPrintNum->SetValue(false);
            m_cbPrintNum->Enable(false);
            m_tiBMAccels->Enable(false);
            m_tiBMSpeeds->Enable(false);
            break;
    }

    if (!isDDE) {
        m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(1.0));

        if (m_params.mode == CalibMode::Calib_PA_Pattern) {
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.05));
        } else {
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.02));
        }
    }
}

void PA_Calibration_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    read_double = m_tiStartPA->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiEndPA->GetTextCtrl()->GetValue().ToDouble(&m_params.end);
    read_double = read_double && m_tiPAStep->GetTextCtrl()->GetValue().ToDouble(&m_params.step);
    if (!read_double || m_params.start < 0 || m_params.step < 10 * EPSILON || m_params.end < m_params.start + m_params.step) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nStart PA: >= 0.0\nEnd PA: > Start PA\nPA step: >= 0.001"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    switch (m_rbMethod->GetSelection()) {
        case 1:
            m_params.mode = CalibMode::Calib_PA_Line;
            break;
        case 2:
            m_params.mode = CalibMode::Calib_PA_Pattern;
            break;
        default:
            m_params.mode = CalibMode::Calib_PA_Tower;
    }

    // ORCA-Belt: backstop in case the selection slipped past the UI guards.
    if (is_belt_printer_selected() && m_params.mode != CalibMode::Calib_PA_Tower) {
        MessageDialog msg_dlg(nullptr, _L("PA Line and PA Pattern tests are not enabled yet on belt printers.\nPlease use the PA Tower method instead."),
                              wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    m_params.print_numbers = m_cbPrintNum->GetValue();
    ParseStringValues(m_tiBMAccels->GetTextCtrl()->GetValue().ToStdString(), m_params.accelerations);
    ParseStringValues(m_tiBMSpeeds->GetTextCtrl()->GetValue().ToStdString(), m_params.speeds);

    if (!m_params.accelerations.empty() && !m_params.speeds.empty()) {
        // Guard against swapped inputs by ensuring acceleration magnitudes exceed speeds.
        const double min_accel = *std::min_element(m_params.accelerations.begin(), m_params.accelerations.end());
        const double max_speed = *std::max_element(m_params.speeds.begin(), m_params.speeds.end());
        if (min_accel <= max_speed) {
            MessageDialog msg_dlg(nullptr,
                _L("Acceleration values must be greater than speed values.\nPlease verify the inputs."),
                wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }
    }

    m_filament->apply_to(m_params);
    m_plater->calib_pa(m_params);
    EndModal(wxID_OK);

}
void PA_Calibration_Dlg::on_extruder_type_changed(wxCommandEvent& event) {
    PA_Calibration_Dlg::reset_params();
    event.Skip();
}
void PA_Calibration_Dlg::on_method_changed(wxCommandEvent& event) {
    // ORCA-Belt: only the PA Tower method is enabled on belt printers so far.
    if (is_belt_printer_selected() && m_rbMethod->GetSelection() != 0)
        m_rbMethod->SetSelection(0, true);
    PA_Calibration_Dlg::reset_params();
    event.Skip();
}

void PA_Calibration_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();
}

void PA_Calibration_Dlg::on_show(wxShowEvent& event) {
    // ORCA-Belt: the dialog is cached across printer switches, so refresh the
    // belt restriction on every show.
    if (is_belt_printer_selected()) {
        m_rbMethod->SetSelection(0);
        const wxString tip = _L("Not enabled yet on belt printers — use the PA Tower method instead.");
        m_rbMethod->SetRadioTooltip(1, tip);
        m_rbMethod->SetRadioTooltip(2, tip);
    } else {
        m_rbMethod->SetRadioTooltip(1, wxEmptyString);
        m_rbMethod->SetRadioTooltip(2, wxEmptyString);
    }
    PA_Calibration_Dlg::reset_params();
    event.Skip();
}

// Temp calib dlg
//
Temp_Calibration_Dlg::Temp_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Temperature calibration"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);
    m_filament = std::make_unique<CalibFilamentSelector>(this);
    v_sizer->Add(m_filament->sizer(), 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Belt temperature-tower model: Standard (sectioned tower) vs Overhang (engraved
    // inverted-L provini that stress overhang quality per temperature). Only affects
    // belt printers; the upright tower ignores it, so the picker is only shown on
    // belts. The dialog is cached across printer switches, so visibility is toggled
    // per-show in on_show() rather than gated here at construction time.
    auto labeled_box_model = new LabeledStaticBox(this, _L("Test model"));
    m_model_box = new wxStaticBoxSizer(labeled_box_model, wxHORIZONTAL);
    m_rbModel = new RadioGroup(this, { _L_CONTEXT("Standard", "Test model"), _L_CONTEXT("Overhang", "Test model") }, wxVERTICAL);
    m_model_box->Add(m_rbModel, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(m_model_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString start_temp_str = _L("Start temp: ");
    wxString end_temp_str   = _L("End temp: ");
    wxString temp_step_str  = _L("Temp step: ");
    int text_max = GetTextMax(this, std::vector<wxString>{start_temp_str, end_temp_str, temp_step_str});

    auto st_size = wxSize(text_max, -1);
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // start temp
    auto start_temp_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_temp_text = new wxStaticText(this, wxID_ANY, start_temp_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStart = new TextInput(this, std::to_string(230), _L("\u2103" /* °C */), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    start_temp_sizer->Add(start_temp_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_temp_sizer->Add(m_tiStart      , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_temp_sizer, 0, wxLEFT, FromDIP(3));

    // end temp
    auto end_temp_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_temp_text = new wxStaticText(this, wxID_ANY, end_temp_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiEnd = new TextInput(this, std::to_string(190), _L("\u2103" /* °C */), "", wxDefaultPosition, ti_size);
    m_tiEnd->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_temp_sizer->Add(end_temp_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    end_temp_sizer->Add(m_tiEnd      , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_temp_sizer, 0, wxLEFT, FromDIP(3));

    // temp step
    auto temp_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto temp_step_text = new wxStaticText(this, wxID_ANY, temp_step_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStep = new TextInput(this, wxString::FromDouble(5), _L("\u2103" /* °C */), "", wxDefaultPosition, ti_size);
    m_tiStep->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_tiStep->Enable(false);
    temp_step_sizer->Add(temp_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    temp_step_sizer->Add(m_tiStep      , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(temp_step_sizer, 0, wxLEFT, FromDIP(3));

    // Resize the model to the nozzle diameter (recommended)
    m_cbResize = add_scale_checkbox(this, settings_sizer);

    settings_sizer->AddSpacer(FromDIP(5));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});

    auto bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto wiki_btn = new Button(this, "", "toolbar_wiki", 0, 15);
    auto wiki_url = "https://www.orcaslicer.com/wiki/temp_calib";
    wiki_btn->SetToolTip(_L("Wiki Guide") + "\n" + wiki_url);
    wiki_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Circle);
    wiki_btn->SetCanFocus(false);
    wiki_btn->Bind(wxEVT_LEFT_DOWN, ([wiki_url](auto& e) {wxLaunchDefaultBrowser(wiki_url);}));

    bottom_sizer->Add(wiki_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    bottom_sizer->AddStretchSpacer();
    bottom_sizer->Add(dlg_btns, 0, wxEXPAND);
    v_sizer->Add(bottom_sizer, 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &Temp_Calibration_Dlg::on_start, this);

    // Start from the recommended range of the filament being calibrated; keep typed temperatures until it changes.
    m_filament->set_on_change([this, last_filament = std::string()]() mutable {
        const Preset*     preset   = m_filament->preset();
        const std::string filament = preset != nullptr ? preset->name : std::string();
        if (filament == last_filament)
            return;
        last_filament = filament;
        // In the tower's 5 °C steps, or PLA's range when the filament's leaves no room for a test.
        int         start_temp = 230;
        int         end_temp   = 190;
        const auto* low        = preset != nullptr ? preset->config.option<ConfigOptionInts>("nozzle_temperature_range_low") : nullptr;
        const auto* high       = preset != nullptr ? preset->config.option<ConfigOptionInts>("nozzle_temperature_range_high") : nullptr;
        if (low != nullptr && high != nullptr && !low->values.empty() && !high->values.empty()) {
            const int lo = std::max((low->values.front() + 4) / 5 * 5, 155);
            const int hi = std::min(high->values.front() / 5 * 5, 500);
            if (hi >= lo + 5) {
                start_temp = hi;
                end_temp   = lo;
            }
        }
        m_tiStart->GetTextCtrl()->SetValue(std::to_string(start_temp));
        m_tiEnd->GetTextCtrl()->SetValue(std::to_string(end_temp));
    });

    // Refresh the belt-only model picker on every show — the dialog is cached and
    // reused across printer switches.
    this->Connect(wxEVT_SHOW, wxShowEventHandler(Temp_Calibration_Dlg::on_show));
    m_model_box->ShowItems(is_belt_printer_selected());

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
    v_sizer->SetSizeHints(this);

    auto validate_text = [](TextInput* ti){
        unsigned long t = 0;
        if(!ti->GetTextCtrl()->GetValue().ToULong(&t))
            return;
        if(t> 500 || t < 155){
            // TRN %s is the temperature unit
            MessageDialog msg_dlg(nullptr, wxString::Format(_L("Supported range: 170%s - 500%s"),
                _L("\u2103" /* °C */), _L("\u2103" /* °C */)),
                wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            if(t > 500)
                t = 500;
            else
                t = 155;
        }
        t = (t / 5) * 5;
        ti->GetTextCtrl()->SetValue(std::to_string(t));
    };

    m_tiStart->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [&](wxFocusEvent &e) {
        validate_text(this->m_tiStart);
        e.Skip();
        });

    m_tiEnd->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [&](wxFocusEvent &e) {
        validate_text(this->m_tiEnd);
        e.Skip();
        });


}

void Temp_Calibration_Dlg::on_start(wxCommandEvent& event) {
    bool read_long = false;
    unsigned long start=0,end=0;
    read_long = m_tiStart->GetTextCtrl()->GetValue().ToULong(&start);
    read_long = read_long && m_tiEnd->GetTextCtrl()->GetValue().ToULong(&end);

    if (!read_long || start > 500 || end < 155  || end > (start - 5)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nStart temp: <= 500\nEnd temp: >= 155\nStart temp >= End temp + 5"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }
    m_params.start = start;
    m_params.end = end;
    m_params.nozzle_based_resize = m_cbResize->GetValue();
    m_params.mode = CalibMode::Calib_Temp_Tower;
    // Picker only exists on belt printers; default non-belt to the Standard model.
    m_params.test_model = m_rbModel ? m_rbModel->GetSelection() : 0;
    m_filament->apply_to(m_params);
    m_plater->calib_temp(m_params);
    EndModal(wxID_OK);

}

void Temp_Calibration_Dlg::on_show(wxShowEvent& event) {
    // ORCA-Belt: the dialog is cached across printer switches, so refresh the
    // belt-only "Test model" picker on every show. The Overhang model only
    // applies to belt printers; hide it (and resize the dialog) otherwise.
    const bool belt = is_belt_printer_selected();
    if (m_model_box->AreAnyItemsShown() != belt) {
        m_model_box->ShowItems(belt);
        Layout();
        Fit();
        GetSizer()->SetSizeHints(this);
    }
    event.Skip();
}

void Temp_Calibration_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();

}


// MaxVolumetricSpeed_Test_Dlg
//

MaxVolumetricSpeed_Test_Dlg::MaxVolumetricSpeed_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Max volumetric speed test"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);
    m_filament = std::make_unique<CalibFilamentSelector>(this);
    v_sizer->Add(m_filament->sizer(), 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString start_vol_str = _L("Start volumetric speed: ");
    wxString end_vol_str   = _L("End volumetric speed: ");
    wxString vol_step_str  = _L_CONTEXT("Step", "Increment") + ": ";
    int text_max = GetTextMax(this, std::vector<wxString>{start_vol_str, end_vol_str, vol_step_str});

    auto st_size = wxSize(text_max, -1);
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // start vol
    auto start_vol_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_vol_text = new wxStaticText(this, wxID_ANY, start_vol_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStart = new TextInput(this, std::to_string(5), _L(u8"mm³/s"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));

    start_vol_sizer->Add(start_vol_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_vol_sizer->Add(m_tiStart     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_vol_sizer, 0, wxLEFT, FromDIP(3));

    // end vol
    auto end_vol_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_vol_text = new wxStaticText(this, wxID_ANY, end_vol_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiEnd = new TextInput(this, std::to_string(20), _L(u8"mm³/s"), "", wxDefaultPosition, ti_size);
    m_tiEnd->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_vol_sizer->Add(end_vol_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    end_vol_sizer->Add(m_tiEnd     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_vol_sizer, 0, wxLEFT, FromDIP(3));

    // vol step
    auto vol_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto vol_step_text = new wxStaticText(this, wxID_ANY, vol_step_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStep = new TextInput(this, wxString::FromDouble(0.5), _L(u8"mm³/s"), "", wxDefaultPosition, ti_size);
    m_tiStep->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    vol_step_sizer->Add(vol_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    vol_step_sizer->Add(m_tiStep     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(vol_step_sizer, 0, wxLEFT, FromDIP(3));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});

    auto bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto wiki_btn = new Button(this, "", "toolbar_wiki", 0, 15);
    auto wiki_url = "https://www.orcaslicer.com/wiki/volumetric_speed_calib";
    wiki_btn->SetToolTip(_L("Wiki Guide") + "\n" + wiki_url);
    wiki_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Circle);
    wiki_btn->SetCanFocus(false);
    wiki_btn->Bind(wxEVT_LEFT_DOWN, ([wiki_url](auto& e) {wxLaunchDefaultBrowser(wiki_url);}));

    bottom_sizer->Add(wiki_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    bottom_sizer->AddStretchSpacer();
    bottom_sizer->Add(dlg_btns, 0, wxEXPAND);
    v_sizer->Add(bottom_sizer, 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &MaxVolumetricSpeed_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
    v_sizer->SetSizeHints(this);
}

MaxVolumetricSpeed_Test_Dlg::~MaxVolumetricSpeed_Test_Dlg() {
    // Disconnect Events
}

void MaxVolumetricSpeed_Test_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    read_double = m_tiStart->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiEnd->GetTextCtrl()->GetValue().ToDouble(&m_params.end);
    read_double = read_double && m_tiStep->GetTextCtrl()->GetValue().ToDouble(&m_params.step);

    if (!read_double || m_params.start <= 0 || m_params.step <= 0 || m_params.end < (m_params.start + m_params.step)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nstart > 0\nstep >= 0\nend > start + step"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    m_params.mode = CalibMode::Calib_Vol_speed_Tower;
    m_filament->apply_to(m_params);
    m_plater->calib_max_vol_speed(m_params);
    EndModal(wxID_OK);

}

void MaxVolumetricSpeed_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();

}


// VFA_Test_Dlg
//

VFA_Test_Dlg::VFA_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("VFA test"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);
    m_filament = std::make_unique<CalibFilamentSelector>(this);
    v_sizer->Add(m_filament->sizer(), 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString start_str    = _L("Start speed: ");
    wxString end_vol_str  = _L("End speed: ");
    wxString vol_step_str = _L_CONTEXT("Step", "Increment") + ": ";
    int text_max = GetTextMax(this, std::vector<wxString>{start_str, end_vol_str, vol_step_str});

    auto st_size = wxSize(text_max, -1);
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // start vol
    auto start_vol_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_vol_text = new wxStaticText(this, wxID_ANY, start_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStart = new TextInput(this, std::to_string(40), _L("mm/s"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));

    start_vol_sizer->Add(start_vol_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_vol_sizer->Add(m_tiStart     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_vol_sizer, 0, wxLEFT, FromDIP(3));

    // end vol
    auto end_vol_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_vol_text = new wxStaticText(this, wxID_ANY, end_vol_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiEnd = new TextInput(this, std::to_string(200), _L("mm/s"), "", wxDefaultPosition, ti_size);
    m_tiEnd->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_vol_sizer->Add(end_vol_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    end_vol_sizer->Add(m_tiEnd     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_vol_sizer, 0, wxLEFT, FromDIP(3));

    // vol step
    auto vol_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto vol_step_text = new wxStaticText(this, wxID_ANY, vol_step_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStep = new TextInput(this, wxString::FromDouble(10), _L("mm/s"), "", wxDefaultPosition, ti_size);
    m_tiStep->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    vol_step_sizer->Add(vol_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    vol_step_sizer->Add(m_tiStep     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(vol_step_sizer, 0, wxLEFT, FromDIP(3));

    // Resize the model to the nozzle diameter (recommended)
    m_cbResize = add_scale_checkbox(this, settings_sizer);

    // Auto-adjust parameters to the filament's max volumetric speed
    auto auto_adjust_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cbAutoAdjust = new LabeledCheckBox(this, _L("Auto-adjust to max volumetric speed"));
    m_cbAutoAdjust->SetValue(true);
    m_cbAutoAdjust->SetToolTip(_L("If the end speed would exceed the filament's maximum volumetric speed, automatically lower the layer "
                                  "height (keeping standard values and staying within the machine's limits) to reach it. If even the "
                                  "minimum layer height is not enough, lower the end speed instead."));
    settings_sizer->Add(m_cbAutoAdjust, 0, wxLEFT | wxTOP, FromDIP(3));

    settings_sizer->AddSpacer(FromDIP(5));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});

    auto bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto wiki_btn = new Button(this, "", "toolbar_wiki", 0, 15);
    auto wiki_url = "https://www.orcaslicer.com/wiki/vfa_calib";
    wiki_btn->SetToolTip(_L("Wiki Guide") + "\n" + wiki_url);
    wiki_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Circle);
    wiki_btn->SetCanFocus(false);
    wiki_btn->Bind(wxEVT_LEFT_DOWN, ([wiki_url](auto& e) {wxLaunchDefaultBrowser(wiki_url);}));

    bottom_sizer->Add(wiki_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    bottom_sizer->AddStretchSpacer();
    bottom_sizer->Add(dlg_btns, 0, wxEXPAND);
    v_sizer->Add(bottom_sizer, 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &VFA_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
    v_sizer->SetSizeHints(this);
}

VFA_Test_Dlg::~VFA_Test_Dlg()
{
    // Disconnect Events
}

void VFA_Test_Dlg::on_start(wxCommandEvent& event)
{
    bool read_double = false;
    read_double = m_tiStart->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiEnd->GetTextCtrl()->GetValue().ToDouble(&m_params.end);
    read_double = read_double && m_tiStep->GetTextCtrl()->GetValue().ToDouble(&m_params.step);

    if (!read_double || m_params.start <= 10 || m_params.step <= 0 || m_params.end < (m_params.start + m_params.step)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nstart > 10\nstep >= 0\nend > start + step"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    // If the requested end speed would exceed the filament's maximum volumetric speed, the slicer clamps the
    // outer wall speed, so the upper blocks of the tower would all print at the same (clamped) speed instead of
    // the requested one. Depending on the "Auto-adjust" option, either fix it automatically or just warn.
    m_filament->apply_to(m_params);
    m_params.vfa_layer_height    = 0.0; // 0 = auto (nozzle/2); overridden below when auto-adjusting
    m_params.nozzle_based_resize = m_cbResize->GetValue();
    if (const auto* preset_bundle = wxGetApp().preset_bundle) {
        const auto& printer_config  = preset_bundle->printers.get_edited_preset().config;
        const auto& print_config    = preset_bundle->prints.get_edited_preset().config;
        const Preset* filament        = m_filament->preset();
        const auto&   filament_config = (filament != nullptr ? *filament : preset_bundle->filaments.get_edited_preset()).config;

        const int extruder_id = std::max(m_params.extruder_id, 0);
        auto get_at = [extruder_id](const ConfigOptionFloats* opt, double fallback) {
            if (opt == nullptr || opt->values.empty())
                return fallback;
            return opt->values[std::min(static_cast<size_t>(extruder_id), opt->values.size() - 1)];
        };

        const double nozzle_diameter = get_at(printer_config.option<ConfigOptionFloats>("nozzle_diameter"), vfa_base_nozzle_diameter);
        double preset_lh = nozzle_diameter / 2.0;
        if (const auto* lh_opt = print_config.option<ConfigOptionFloat>("layer_height"))
            if (lh_opt->value > 0.0)
                preset_lh = lh_opt->value;
        // Layer height the tower will actually print at: nozzle/2 when resizing, else the preset value.
        const double default_lh      = m_params.nozzle_based_resize ? nozzle_diameter / 2.0 : preset_lh;
        const double max_vol_speed   = get_at(filament_config.option<ConfigOptionFloats>("filament_max_volumetric_speed"), 0.0);
        const double machine_min_lh  = get_at(printer_config.option<ConfigOptionFloats>("min_layer_height"), 0.0);
        const double machine_max_lh  = get_at(printer_config.option<ConfigOptionFloats>("max_layer_height"), 0.0);

        double line_width = print_config.get_abs_value("outer_wall_line_width", nozzle_diameter);
        if (line_width <= 0.0)
            line_width = print_config.get_abs_value("line_width", nozzle_diameter);
        if (line_width <= 0.0)
            line_width = nozzle_diameter;

        // Max outer-wall speed printable at a given layer height without exceeding the volumetric limit.
        auto speed_limit_for_lh = [&](double lh) -> double {
            const double mm3_per_mm = Flow(line_width, lh, nozzle_diameter).mm3_per_mm();
            return mm3_per_mm > 0.0 ? max_vol_speed / mm3_per_mm : 1e9;
        };

        auto confirm_clamp = [&](const wxString& question) -> bool {
            MessageDialog msg_dlg(nullptr,
                wxString::Format(_L("The end speed (%.0f mm/s) exceeds the filament's maximum volumetric speed "
                                    "(%.1f mm³/s), which limits the outer wall to about %.0f mm/s at this line width and "
                                    "layer height.\nSpeeds above this will be clamped, so the upper blocks of the tower "
                                    "will not print at the requested speed.\n\n%s"),
                                 m_params.end, max_vol_speed, speed_limit_for_lh(default_lh), question),
                _L("VFA test"), wxICON_WARNING | wxYES_NO | wxNO_DEFAULT);
            return msg_dlg.ShowModal() == wxID_YES;
        };

        if (max_vol_speed > 0.0 && nozzle_diameter > 0.0 && m_params.end > speed_limit_for_lh(default_lh)) {
            // The layer-height auto-adjust only applies when resizing is enabled (it changes the layer height).
            if (m_cbAutoAdjust->GetValue() && m_params.nozzle_based_resize) {
                // Candidate layer heights are the ones actually used by the process profiles compatible with the
                // current printer (clamped to the machine's layer-height limits, when set). A smaller layer height
                // means a smaller cross-section, hence a higher printable speed under the volumetric limit; pick the
                // largest candidate that still reaches the end speed to keep the change from the default minimal.
                std::vector<double> candidates;
                for (const auto& preset : preset_bundle->prints.get_presets()) {
                    if (!preset.is_compatible || preset.is_default)
                        continue;
                    const auto* lh_opt = preset.config.option<ConfigOptionFloat>("layer_height");
                    if (lh_opt == nullptr || lh_opt->value <= 0.0)
                        continue;
                    const double lh = lh_opt->value;
                    if ((machine_min_lh > 0.0 && lh < machine_min_lh - 1e-6) ||
                        (machine_max_lh > 0.0 && lh > machine_max_lh + 1e-6))
                        continue;
                    candidates.push_back(lh);
                }
                std::sort(candidates.begin(), candidates.end());
                candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                             [](double a, double b) { return std::abs(a - b) < 1e-6; }),
                                 candidates.end());

                // Largest candidate <= the default layer height that still reaches the end speed (smallest change).
                double chosen_lh = 0.0;
                for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
                    if (*it > default_lh + 1e-6)
                        continue; // never increase the layer height above the default
                    if (speed_limit_for_lh(*it) >= m_params.end) { chosen_lh = *it; break; }
                }

                if (chosen_lh > 0.0) {
                    // Reducing the layer height is enough to reach the requested end speed.
                    m_params.vfa_layer_height = chosen_lh;
                    MessageDialog msg_dlg(nullptr,
                        wxString::Format(_L("The end speed (%.0f mm/s) exceeds the filament's maximum volumetric speed "
                                            "(%.1f mm³/s) at the default layer height (%.2f mm).\n\n"
                                            "The layer height has been reduced to %.2f mm (a value used by this printer's "
                                            "profiles) so the tower can reach the requested speed."),
                                         m_params.end, max_vol_speed, default_lh, chosen_lh),
                        _L("VFA test"), wxICON_INFORMATION | wxOK);
                    msg_dlg.ShowModal();
                } else if (!candidates.empty()) {
                    // Even the smallest available layer height cannot reach the end speed; propose a lower end speed
                    // based on that layer height, the line width and the maximum volumetric speed.
                    const double min_lh    = candidates.front();
                    const double reachable = speed_limit_for_lh(min_lh);
                    double new_end = std::floor(reachable / m_params.step) * m_params.step; // snap down to a step multiple
                    if (new_end < m_params.start + m_params.step)
                        new_end = m_params.start + m_params.step;
                    MessageDialog msg_dlg(nullptr,
                        wxString::Format(_L("Even at the smallest layer height used by this printer's profiles (%.2f mm) the "
                                            "end speed (%.0f mm/s) exceeds the filament's maximum volumetric speed "
                                            "(%.1f mm³/s).\n\n"
                                            "The layer height will be set to %.2f mm and the end speed lowered to %.0f mm/s.\n\n"
                                            "Continue?"),
                                         min_lh, m_params.end, max_vol_speed, min_lh, new_end),
                        _L("VFA test"), wxICON_WARNING | wxYES_NO | wxNO_DEFAULT);
                    if (msg_dlg.ShowModal() != wxID_YES)
                        return;
                    m_params.end              = new_end;
                    m_params.vfa_layer_height = min_lh;
                } else {
                    // No compatible process profiles to draw layer heights from: warn and let the user decide.
                    if (!confirm_clamp(_L("Continue anyway?")))
                        return;
                }
            } else {
                // Auto-adjust off, or resizing disabled (which forbids changing the layer height): just warn.
                if (!confirm_clamp(m_params.nozzle_based_resize
                        ? _L("Enable \"Auto-adjust\" to fix this automatically, or continue anyway?")
                        : _L("Enable \"Auto-scale for nozzle\" and \"Auto-adjust\" to fix this automatically, or continue anyway?")))
                    return;
            }
        }
    }

    m_params.mode = CalibMode::Calib_VFA_Tower;
    m_plater->calib_VFA(m_params);
    EndModal(wxID_OK);
}

void VFA_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect)
{
    this->Refresh();
    Fit();
}



// Retraction_Test_Dlg
//

Retraction_Test_Dlg::Retraction_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Retraction"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);
    m_filament = std::make_unique<CalibFilamentSelector>(this);
    v_sizer->Add(m_filament->sizer(), 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString start_length_str = _L("Start retraction length: ");
    wxString end_length_str   = _L("End retraction length: ");
    wxString length_step_str  = _L_CONTEXT("Step", "Increment") + ": ";
    int text_max = GetTextMax(this, std::vector<wxString>{start_length_str, end_length_str, length_step_str});

    auto st_size = wxSize(text_max, -1);
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // start length
    auto start_length_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_length_text = new wxStaticText(this, wxID_ANY, start_length_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStart = new TextInput(this, std::to_string(0), _L("mm"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));

    start_length_sizer->Add(start_length_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_length_sizer->Add(m_tiStart        , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_length_sizer, 0, wxLEFT, FromDIP(3));

    // end length
    auto end_length_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_length_text = new wxStaticText(this, wxID_ANY, end_length_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiEnd = new TextInput(this, std::to_string(2), _L("mm"), "", wxDefaultPosition, ti_size);
    m_tiEnd->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_length_sizer->Add(end_length_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    end_length_sizer->Add(m_tiEnd        , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_length_sizer, 0, wxLEFT, FromDIP(3));

    // length step
    auto length_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto length_step_text = new wxStaticText(this, wxID_ANY, length_step_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStep = new TextInput(this, wxString::FromDouble(0.1), _L("mm"), "", wxDefaultPosition, ti_size);
    m_tiStep->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    length_step_sizer->Add(length_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    length_step_sizer->Add(m_tiStep        , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(length_step_sizer, 0, wxLEFT, FromDIP(3));

    settings_sizer->AddSpacer(FromDIP(5));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});

    auto bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto wiki_btn = new Button(this, "", "toolbar_wiki", 0, 15);
    auto wiki_url = "https://www.orcaslicer.com/wiki/retraction_calib";
    wiki_btn->SetToolTip(_L("Wiki Guide") + "\n" + wiki_url);
    wiki_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Circle);
    wiki_btn->SetCanFocus(false);
    wiki_btn->Bind(wxEVT_LEFT_DOWN, ([wiki_url](auto& e) {wxLaunchDefaultBrowser(wiki_url);}));

    bottom_sizer->Add(wiki_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    bottom_sizer->AddStretchSpacer();
    bottom_sizer->Add(dlg_btns, 0, wxEXPAND);
    v_sizer->Add(bottom_sizer, 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &Retraction_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
    v_sizer->SetSizeHints(this);
}

Retraction_Test_Dlg::~Retraction_Test_Dlg() {
    // Disconnect Events
}

void Retraction_Test_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    read_double = m_tiStart->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiEnd->GetTextCtrl()->GetValue().ToDouble(&m_params.end);
    read_double = read_double && m_tiStep->GetTextCtrl()->GetValue().ToDouble(&m_params.step);

    if (!read_double || m_params.start < 0 || m_params.step <= 0 || m_params.end < (m_params.start + m_params.step)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nstart > 0\nstep >= 0\nend > start + step"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    m_params.mode = CalibMode::Calib_Retraction_tower;
    m_filament->apply_to(m_params);
    m_plater->calib_retraction(m_params);
    EndModal(wxID_OK);

}

void Retraction_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();

}

// Input_Shaping_Freq_Test_Dlg
//

Input_Shaping_Freq_Test_Dlg::Input_Shaping_Freq_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Input shaping Frequency test"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    const auto* preset_bundle = wxGetApp().preset_bundle;
    const auto* gcode_flavor_option = (preset_bundle != nullptr)
        ? preset_bundle->printers.get_edited_preset().config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")
        : nullptr;
    const bool reprap_firmware = gcode_flavor_option && gcode_flavor_option->value == GCodeFlavor::gcfRepRapFirmware;

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);
    m_filament = std::make_unique<CalibFilamentSelector>(this);
    v_sizer->Add(m_filament->sizer(), 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Model selection
    auto labeled_box_model = new LabeledStaticBox(this, _L("Test model"));
    auto model_box = new wxStaticBoxSizer(labeled_box_model, wxHORIZONTAL);

    m_rbModel = new RadioGroup(this, { _L("Ringing Tower"), _L("Fast Tower") }, wxVERTICAL);
    model_box->Add(m_rbModel, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(model_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Input shaper type selection
    auto labeled_box_type = new LabeledStaticBox(this, _L("Input shaper type"));
    auto type_box = new wxStaticBoxSizer(labeled_box_type, wxVERTICAL);
    auto type_labels = make_shaper_type_labels();
    m_rbType = new RadioGroup(this, type_labels, wxVERTICAL, 3);
    type_box->Add(m_rbType, 0, wxALL | wxEXPAND, FromDIP(4));
    m_rbType->SetSelection(0);

    // Determine firmware-specific note
    wxString firmware_note = _L("Please ensure the selected type is compatible with your firmware version.");
    if (gcode_flavor_option) {
        switch (gcode_flavor_option->value) {
        case GCodeFlavor::gcfMarlinFirmware:
        case GCodeFlavor::gcfMarlinLegacy:
            firmware_note = _L("Marlin version => 2.1.2\nFixed-Time motion not yet implemented.");
            break;
        case GCodeFlavor::gcfKlipper:
            firmware_note = _L("Klipper version => 0.9.0");
            break;
        case GCodeFlavor::gcfRepRapFirmware:
            firmware_note = _L("RepRapFirmware version => 3.4.0\nCheck your firmware documentation for supported shaper types.");
            break;
        default:
            break;
        }
    }

    auto type_note = new wxStaticText(this, wxID_ANY, firmware_note, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    type_note->SetForegroundColour(wxColour(128, 128, 128));
    type_box->Add(type_note, 0, wxALL, FromDIP(5));

    v_sizer->Add(type_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString x_axis_str = reprap_firmware ? _L("Frequency (Start / End): ") : _L("X Start / End: ");
    wxString y_axis_str = _L("Y Start / End: ");
    int text_max = GetTextMax(this, std::vector<wxString>{x_axis_str, y_axis_str});

    auto st_size = wxSize(text_max, -1);
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Frequency settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // X axis frequencies
    auto x_freq_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_x_text = new wxStaticText(this, wxID_ANY, x_axis_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiFreqStartX = new TextInput(this, std::to_string(15) , _L("Hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqStartX->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_tiFreqEndX   = new TextInput(this, std::to_string(110), _L("Hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqEndX->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    
    x_freq_sizer->Add(start_x_text  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    x_freq_sizer->Add(m_tiFreqStartX, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    x_freq_sizer->Add(m_tiFreqEndX  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(x_freq_sizer, 0, wxLEFT, FromDIP(3));

    // Y axis frequencies
    auto y_freq_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_y_text = new wxStaticText(this, wxID_ANY, y_axis_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiFreqStartY = new TextInput(this, std::to_string(15) , _L("Hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqStartY->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_tiFreqEndY =   new TextInput(this, std::to_string(110), _L("Hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqEndY->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));

    y_freq_sizer->Add(start_y_text  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    y_freq_sizer->Add(m_tiFreqStartY, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    y_freq_sizer->Add(m_tiFreqEndY  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(y_freq_sizer, 0, wxLEFT, FromDIP(3));

    if (reprap_firmware) {
        m_tiFreqStartY->GetTextCtrl()->SetValue(m_tiFreqStartX->GetTextCtrl()->GetValue());
        m_tiFreqEndY->GetTextCtrl()->SetValue(m_tiFreqEndX->GetTextCtrl()->GetValue());
        start_y_text->Hide();
        m_tiFreqStartY->Hide();
        m_tiFreqEndY->Hide();
        settings_sizer->Hide(y_freq_sizer);
        start_x_text->SetLabel(_L("Frequency (Start / End): "));
        m_tiFreqStartX->GetTextCtrl()->SetToolTip(_L("RepRapFirmware uses the same frequency range for both axes."));
        m_tiFreqEndX->GetTextCtrl()->SetToolTip(_L("RepRapFirmware uses the same frequency range for both axes."));
    }

    // Damping Factor
    wxString damping_factor_str = _L("Damp: ");
    auto damping_factor_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto damping_factor_text = new wxStaticText(this, wxID_ANY, damping_factor_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiDampingFactor = new TextInput(this, wxString::Format("%.3f", 0.15), "", "", wxDefaultPosition, ti_size);
    m_tiDampingFactor->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    
    damping_factor_sizer->Add(damping_factor_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    damping_factor_sizer->Add(m_tiDampingFactor  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(damping_factor_sizer, 0, wxLEFT, FromDIP(3));
    
    settings_sizer->AddSpacer(FromDIP(5));

    auto note_text = new wxStaticText(this, wxID_ANY, _L("Recommended: Set Damp to 0.\nThis will use the printer's default or saved value."), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    note_text->SetForegroundColour(wxColour(128, 128, 128));
    note_text->Wrap(FromDIP(350));
    settings_sizer->Add(note_text, 0, wxALL, FromDIP(5));

    settings_sizer->AddSpacer(FromDIP(5));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});

    auto bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto wiki_btn = new Button(this, "", "toolbar_wiki", 0, 15);
    auto wiki_url = "https://www.orcaslicer.com/wiki/input_shaping_calib";
    wiki_btn->SetToolTip(_L("Wiki Guide") + "\n" + wiki_url);
    wiki_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Circle);
    wiki_btn->SetCanFocus(false);
    wiki_btn->Bind(wxEVT_LEFT_DOWN, ([wiki_url](auto& e) {wxLaunchDefaultBrowser(wiki_url);}));

    bottom_sizer->Add(wiki_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    bottom_sizer->AddStretchSpacer();
    bottom_sizer->Add(dlg_btns, 0, wxEXPAND);
    v_sizer->Add(bottom_sizer, 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &Input_Shaping_Freq_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
    v_sizer->SetSizeHints(this);
}

Input_Shaping_Freq_Test_Dlg::~Input_Shaping_Freq_Test_Dlg() {
    // Disconnect Events
}

void Input_Shaping_Freq_Test_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    read_double = m_tiFreqStartX->GetTextCtrl()->GetValue().ToDouble(&m_params.freqStartX);
    read_double = read_double && m_tiFreqEndX->GetTextCtrl()->GetValue().ToDouble(&m_params.freqEndX);

    const auto* preset_bundle = wxGetApp().preset_bundle;
    const auto* gcode_flavor_option = (preset_bundle != nullptr)
        ? preset_bundle->printers.get_edited_preset().config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")
        : nullptr;
    const bool reprap_firmware = gcode_flavor_option && gcode_flavor_option->value == GCodeFlavor::gcfRepRapFirmware;

    if (!reprap_firmware) {
        read_double = read_double && m_tiFreqStartY->GetTextCtrl()->GetValue().ToDouble(&m_params.freqStartY);
        read_double = read_double && m_tiFreqEndY->GetTextCtrl()->GetValue().ToDouble(&m_params.freqEndY);
    } else {
        m_params.freqStartY = m_params.freqStartX;
        m_params.freqEndY   = m_params.freqEndX;
        m_tiFreqStartY->GetTextCtrl()->SetValue(m_tiFreqStartX->GetTextCtrl()->GetValue());
        m_tiFreqEndY->GetTextCtrl()->SetValue(m_tiFreqEndX->GetTextCtrl()->GetValue());
    }
    read_double = read_double && m_tiDampingFactor->GetTextCtrl()->GetValue().ToDouble(&m_params.start);

    if (!read_double ||
        m_params.freqStartX < 0 || m_params.freqEndX > 500 ||
        (!reprap_firmware && (m_params.freqStartY < 0 || m_params.freqEndY > 500)) ||
        m_params.freqStartX >= m_params.freqEndX ||
        (!reprap_firmware && m_params.freqStartY >= m_params.freqEndY)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\n(0 < FreqStart < FreqEnd < 500)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    if (m_params.start < 0 || m_params.start >= 1) {
        MessageDialog msg_dlg(nullptr, _L("Please input a valid damping factor (0 < Damping/zeta factor <= 1)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    auto shaper_values = get_shaper_type_values();
    int type_selection = m_rbType->GetSelection();
    if (shaper_values.empty()) {
        m_params.shaper_type.clear();
    } else {
        if (type_selection < 0 || type_selection >= static_cast<int>(shaper_values.size()))
            type_selection = 0;
        m_params.shaper_type = shaper_values[static_cast<size_t>(type_selection)];
    }

    m_params.mode = CalibMode::Calib_Input_shaping_freq;
    
    // Set model type based on selection
    m_params.test_model = m_rbModel->GetSelection() == 0 ? 0 : 1; // 0 = Ringing Tower, 1 = Fast Tower
    
    m_filament->apply_to(m_params);
    m_plater->calib_input_shaping_freq(m_params);
    EndModal(wxID_OK);
}

void Input_Shaping_Freq_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();
}

// Input_Shaping_Damp_Test_Dlg
//

Input_Shaping_Damp_Test_Dlg::Input_Shaping_Damp_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Input shaping Damp test"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    const auto* preset_bundle = wxGetApp().preset_bundle;
    const auto* gcode_flavor_option = (preset_bundle != nullptr)
        ? preset_bundle->printers.get_edited_preset().config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")
        : nullptr;
    const bool reprap_firmware = gcode_flavor_option && gcode_flavor_option->value == GCodeFlavor::gcfRepRapFirmware;

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);
    m_filament = std::make_unique<CalibFilamentSelector>(this);
    v_sizer->Add(m_filament->sizer(), 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Model selection
    auto labeled_box_model = new LabeledStaticBox(this, _L("Test model"));
    auto model_box = new wxStaticBoxSizer(labeled_box_model, wxHORIZONTAL);

    m_rbModel = new RadioGroup(this, { _L("Ringing Tower"), _L("Fast Tower") }, wxVERTICAL);
    model_box->Add(m_rbModel, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(model_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Input shaper type selection
    auto labeled_box_type = new LabeledStaticBox(this, _L("Input shaper type"));
    auto type_box = new wxStaticBoxSizer(labeled_box_type, wxVERTICAL);
    auto type_labels = make_shaper_type_labels();
    m_rbType = new RadioGroup(this, type_labels, wxVERTICAL, 3);
    type_box->Add(m_rbType, 0, wxALL | wxEXPAND, FromDIP(4));
    m_rbType->SetSelection(0);

    // Determine firmware-specific note
    wxString firmware_note = _L("Check firmware compatibility.");
    if (gcode_flavor_option) {
        switch (gcode_flavor_option->value) {
        case GCodeFlavor::gcfMarlinFirmware:
        case GCodeFlavor::gcfMarlinLegacy:
            firmware_note = _L("Marlin version => 2.1.2\nFixed-Time motion not yet implemented.");
            break;
        case GCodeFlavor::gcfKlipper:
            firmware_note = _L("Klipper version => 0.9.0");
            break;
        case GCodeFlavor::gcfRepRapFirmware:
            firmware_note = _L("RepRapFirmware version => 3.4.0\nCheck your firmware documentation for supported shaper types.");
            break;
        default:
            break;
        }
    }

    auto type_note = new wxStaticText(this, wxID_ANY, firmware_note, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    type_note->SetForegroundColour(wxColour(128, 128, 128));
    type_box->Add(type_note, 0, wxALL, FromDIP(5));

    v_sizer->Add(type_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString freq_str = reprap_firmware ? _L("Frequency: ") : _L("Frequency X / Y: ");
    wxString damp_str = _L("Damp (Start / End): ");
    int text_max = GetTextMax(this, std::vector<wxString>{freq_str, damp_str});

    auto st_size = wxSize(text_max, -1);
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Frequency settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    auto freq_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto freq_text = new wxStaticText(this, wxID_ANY, freq_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiFreqX = new TextInput(this, std::to_string(30), _L("Hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqX->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_tiFreqY = new TextInput(this, std::to_string(30), _L("Hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqY->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    freq_sizer->Add(freq_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    freq_sizer->Add(m_tiFreqX, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    freq_sizer->Add(m_tiFreqY, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(freq_sizer, 0, wxLEFT, FromDIP(3));

    if (reprap_firmware) {
        m_tiFreqY->GetTextCtrl()->SetValue(m_tiFreqX->GetTextCtrl()->GetValue());
        m_tiFreqY->Hide();
        freq_text->SetLabel(freq_str);
        m_tiFreqX->GetTextCtrl()->SetToolTip(_L("RepRapFirmware uses the same frequency for both axes."));
    }

    // Damping Factor Start and End
    auto damp_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto damp_text = new wxStaticText(this, wxID_ANY, damp_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiDampingFactorStart = new TextInput(this, wxString::Format("%.3f", 0.00), "", "", wxDefaultPosition, ti_size);
    m_tiDampingFactorStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_tiDampingFactorEnd   = new TextInput(this, wxString::Format("%.3f", 0.40), "", "", wxDefaultPosition, ti_size);
    m_tiDampingFactorEnd->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    damp_sizer->Add(damp_text             , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    damp_sizer->Add(m_tiDampingFactorStart, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    damp_sizer->Add(m_tiDampingFactorEnd  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(damp_sizer, 0, wxLEFT, FromDIP(3));

    settings_sizer->AddSpacer(FromDIP(5));

    // Add a note to explain users to use their previously calculated frequency
    auto note_text = new wxStaticText(this, wxID_ANY, _L("Note: Use previously calculated frequencies."), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    note_text->SetForegroundColour(wxColour(128, 128, 128));
    settings_sizer->Add(note_text, 0, wxALL, FromDIP(5));

    settings_sizer->AddSpacer(FromDIP(5));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});

    auto bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto wiki_btn = new Button(this, "", "toolbar_wiki", 0, 15);
    auto wiki_url = "https://www.orcaslicer.com/wiki/input_shaping_calib";
    wiki_btn->SetToolTip(_L("Wiki Guide") + "\n" + wiki_url);
    wiki_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Circle);
    wiki_btn->SetCanFocus(false);
    wiki_btn->Bind(wxEVT_LEFT_DOWN, ([wiki_url](auto& e) {wxLaunchDefaultBrowser(wiki_url);}));

    bottom_sizer->Add(wiki_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    bottom_sizer->AddStretchSpacer();
    bottom_sizer->Add(dlg_btns, 0, wxEXPAND);
    v_sizer->Add(bottom_sizer, 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &Input_Shaping_Damp_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
    v_sizer->SetSizeHints(this);
}

Input_Shaping_Damp_Test_Dlg::~Input_Shaping_Damp_Test_Dlg() {
    // Disconnect Events
}

void Input_Shaping_Damp_Test_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    read_double = m_tiFreqX->GetTextCtrl()->GetValue().ToDouble(&m_params.freqStartX);
    const auto* preset_bundle = wxGetApp().preset_bundle;
    const auto* gcode_flavor_option = (preset_bundle != nullptr)
        ? preset_bundle->printers.get_edited_preset().config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")
        : nullptr;
    const bool reprap_firmware = gcode_flavor_option && gcode_flavor_option->value == GCodeFlavor::gcfRepRapFirmware;

    if (!reprap_firmware) {
        read_double = read_double && m_tiFreqY->GetTextCtrl()->GetValue().ToDouble(&m_params.freqStartY);
    } else {
        m_params.freqStartY = m_params.freqStartX;
        m_tiFreqY->GetTextCtrl()->SetValue(m_tiFreqX->GetTextCtrl()->GetValue());
    }
    read_double = read_double && m_tiDampingFactorStart->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiDampingFactorEnd->GetTextCtrl()->GetValue().ToDouble(&m_params.end);

    if (!read_double ||
        m_params.freqStartX < 0 || m_params.freqStartX > 500 ||
        (!reprap_firmware && (m_params.freqStartY < 0 || m_params.freqStartY > 500))) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\n(0 < Freq < 500)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    if (m_params.start < 0 || m_params.end > 1
        || m_params.start >= m_params.end) {
        MessageDialog msg_dlg(nullptr, _L("Please input a valid damping factor (0 <= DampingStart < DampingEnd <= 1)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    auto shaper_values = get_shaper_type_values();
    int type_selection = m_rbType->GetSelection();
    if (shaper_values.empty()) {
        m_params.shaper_type.clear();
    } else {
        if (type_selection < 0 || type_selection >= static_cast<int>(shaper_values.size()))
            type_selection = 0;
        m_params.shaper_type = shaper_values[static_cast<size_t>(type_selection)];
    }

    m_params.mode = CalibMode::Calib_Input_shaping_damp;
    
    // Set model type based on selection
    m_params.test_model = m_rbModel->GetSelection() == 0 ? 0 : 1; // 0 = Ringing Tower, 1 = Fast Tower
    
    m_filament->apply_to(m_params);
    m_plater->calib_input_shaping_damp(m_params);
    EndModal(wxID_OK);
}

void Input_Shaping_Damp_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();
}

// Cornering_Test_Dlg
//

Cornering_Test_Dlg::Cornering_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Cornering test"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);
    m_filament = std::make_unique<CalibFilamentSelector>(this);
    v_sizer->Add(m_filament->sizer(), 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Model selection
    auto labeled_box_model = new LabeledStaticBox(this, _L("Test model"));
    auto model_box = new wxStaticBoxSizer(labeled_box_model, wxHORIZONTAL);

    m_rbModel = new RadioGroup(this, { _L("Ringing Tower"), _L("Fast Tower"), _L("SCV-V2") }, wxVERTICAL);
    model_box->Add(m_rbModel, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(model_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString start_jd_str = _L("Start: ");
    wxString end_jd_str   = _L("End: ");
    int text_max = GetTextMax(this, std::vector<wxString>{start_jd_str, end_jd_str});

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Cornering settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // Detect GCode Flavor and set appropriate values and units
    const auto* preset_bundle = wxGetApp().preset_bundle;
    const auto* gcode_flavor_option = (preset_bundle != nullptr)
        ? preset_bundle->printers.get_edited_preset().config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")
        : nullptr;

    wxString start_value_str;
    wxString end_value_str;
    wxString units_str;

    if (gcode_flavor_option &&
        gcode_flavor_option->value == GCodeFlavor::gcfMarlinFirmware &&
        preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("machine_max_junction_deviation") &&
        !preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("machine_max_junction_deviation")->values.empty() &&
        preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("machine_max_junction_deviation")->values[0] > 0) {
            // Using Junction Deviation (mm)
            start_value_str = wxString::Format("%.3f", 0.000);
            end_value_str   = wxString::Format("%.3f", 0.250);
            units_str = "mm";
        } else {
            // Using Classic Jerk (mm/s)
                start_value_str = wxString::Format("%.3f", 1.0);
                end_value_str   = wxString::Format("%.3f", 15.0);
                units_str = "mm/s";
        }

    auto st_size = wxSize(text_max, -1);
    auto ti_size = FromDIP(wxSize(120, -1));

    // Start cornering
    auto start_jd_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_jd_text = new wxStaticText(this, wxID_ANY, start_jd_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiJDStart = new TextInput(this, start_value_str, units_str, "", wxDefaultPosition, ti_size);
    m_tiJDStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    start_jd_sizer->Add(start_jd_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_jd_sizer->Add(m_tiJDStart  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_jd_sizer, 0, wxLEFT, FromDIP(3));

    // End cornering
    auto end_jd_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_jd_text = new wxStaticText(this, wxID_ANY, end_jd_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiJDEnd = new TextInput(this, end_value_str, units_str, "", wxDefaultPosition, ti_size);
    m_tiJDEnd->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_jd_sizer->Add(end_jd_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
       end_jd_sizer->Add(m_tiJDEnd  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_jd_sizer, 0, wxLEFT, FromDIP(3));

    settings_sizer->AddSpacer(FromDIP(5));

    // Add note about cornering based on GCode Flavor
    wxString note_msg = _L("Note: Lower values = sharper corners but slower speeds.");
    auto note_text = new wxStaticText(this, wxID_ANY, note_msg, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    note_text->SetForegroundColour(wxColour(128, 128, 128));
    note_text->Wrap(FromDIP(300));
    settings_sizer->Add(note_text, 0, wxALL, FromDIP(5));

    if (gcode_flavor_option) {
        wxString note_msg_2;
        switch (gcode_flavor_option->value) {
            case GCodeFlavor::gcfMarlinFirmware: {
                // Check if machine_max_junction_deviation is set and > 0
                const auto* max_jd_option = preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("machine_max_junction_deviation");
                if (max_jd_option && !max_jd_option->values.empty() && max_jd_option->values[0] > 0) {
                    note_msg_2 += _L("Marlin 2 Junction Deviation detected:\nTo test Classic Jerk, set 'Maximum Junction Deviation' in Motion ability to 0.");
                } else {
                    note_msg_2 += _L("Marlin 2 Classic Jerk detected:\nTo test Junction Deviation, set 'Maximum Junction Deviation' in Motion ability to a value > 0.");
                }
                break;
            }
            case GCodeFlavor::gcfRepRapFirmware:
                note_msg_2 += _L("RepRap detected: Jerk in mm/s.\nOrcaSlicer will convert the values to mm/min when necessary.");
                break;
            default:
                break;
        }
        if(!note_msg_2.empty()){
            auto note_text_2 = new wxStaticText(this, wxID_ANY, note_msg_2, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
            note_text_2->SetForegroundColour(wxColour(128, 128, 128));
            note_text_2->Wrap(FromDIP(300));
            settings_sizer->Add(note_text_2, 0, wxALL, FromDIP(5));
        }
    }

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});

    auto bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto wiki_btn = new Button(this, "", "toolbar_wiki", 0, 15);
    auto wiki_url = "https://www.orcaslicer.com/wiki/cornering_calib";
    wiki_btn->SetToolTip(_L("Wiki Guide") + "\n" + wiki_url);
    wiki_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Circle);
    wiki_btn->SetCanFocus(false);
    wiki_btn->Bind(wxEVT_LEFT_DOWN, ([wiki_url](auto& e) {wxLaunchDefaultBrowser(wiki_url);}));

    bottom_sizer->Add(wiki_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    bottom_sizer->AddStretchSpacer();
    bottom_sizer->Add(dlg_btns, 0, wxEXPAND);
    v_sizer->Add(bottom_sizer, 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &Cornering_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
    v_sizer->SetSizeHints(this);
}

Cornering_Test_Dlg::~Cornering_Test_Dlg() {
    // Disconnect Events
}

void Cornering_Test_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    read_double = m_tiJDStart->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiJDEnd->GetTextCtrl()->GetValue().ToDouble(&m_params.end);

    // Get max values based on GCode Flavor
    double max_end_value = 100.0;
    double warning_threshold = 20.0;
    const auto* preset_bundle = wxGetApp().preset_bundle;
    const auto* gcode_flavor_option = (preset_bundle != nullptr)
        ? preset_bundle->printers.get_edited_preset().config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")
        : nullptr;

    if (gcode_flavor_option &&
        gcode_flavor_option->value == GCodeFlavor::gcfMarlinFirmware &&
        preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("machine_max_junction_deviation") &&
        !preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("machine_max_junction_deviation")->values.empty() &&
        preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("machine_max_junction_deviation")->values[0] > 0) {
            // Using Junction Deviation (mm)
            max_end_value = 0.3;
            warning_threshold = 0.25;
    }

    if (!read_double || m_params.start < 0 || m_params.end > max_end_value || m_params.start >= m_params.end) {
        wxString error_msg = wxString::Format(_L("Please input valid values:\n(0 <= Cornering <= %s)"), wxString::Format("%.3f", max_end_value));
        MessageDialog msg_dlg(nullptr, error_msg, wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    } else if (m_params.end > warning_threshold) {
        wxString warning_msg = wxString::Format(_L("NOTE: High values may cause Layer shift (>%s)"), wxString::Format("%.3f", warning_threshold));
        MessageDialog msg_dlg(nullptr, warning_msg, wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
    }

    m_params.mode = CalibMode::Calib_Cornering;

    // Set model type based on selection
    m_params.test_model = m_rbModel->GetSelection();

    m_filament->apply_to(m_params);
    m_plater->Calib_Cornering(m_params);
    EndModal(wxID_OK);
}

void Cornering_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();
}

// FlowRateCalibrationDialog
//
FlowRateCalibrationDialog::FlowRateCalibrationDialog(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Flow Ratio Calibration"), wxDefaultPosition, parent->FromDIP(wxSize(-1, 280)), wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);
    m_filament = std::make_unique<CalibFilamentSelector>(this);
    v_sizer->Add(m_filament->sizer(), 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Type selection
    auto labeled_box_type = new LabeledStaticBox(this, _L("Calibration Test Type"));
    auto type_box = new wxStaticBoxSizer(labeled_box_type, wxVERTICAL);

    m_rbType = new RadioGroup(this, { _L("Pass 1 (Coarse)"), _L("Pass 2 (Fine)"), _L("YOLO (Recommended)"), _L("YOLO (Perfectionist)") }, wxVERTICAL, 1);
    m_rbType->SetSelection(2); // Default to YOLO Recommended
    type_box->Add(m_rbType, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(type_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    auto stb = new LabeledStaticBox(this, _L("Settings"));
    auto settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    wxString pattern_str = _L("Top Surface Pattern");
    int text_max = GetTextMax(this, std::vector<wxString>{pattern_str});

    settings_sizer->AddSpacer(FromDIP(5));

    auto st_size = wxSize(text_max, -1);
    auto ti_size = FromDIP(wxSize(120, -1));

    // Top surface pattern
    auto pattern_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto pattern_text  = new wxStaticText(this, wxID_ANY, pattern_str, wxDefaultPosition, st_size, wxALIGN_LEFT);

    // ORCA: Use ComboBox with icons instead of RadioGroup
    m_rbPattern = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, ti_size, 0, nullptr, wxCB_READONLY);
    
    boost::filesystem::path image_path(Slic3r::resources_dir());
    image_path /= "images";

    auto add_pattern_item = [&](const std::string& name, const wxString& label) {
        auto icon_name = "param_" + name;
        if (boost::filesystem::exists(image_path / (icon_name + ".svg"))) {
            // Using 24px icon size to match other settings (Field.cpp uses 24)
            ScalableBitmap bm(this, icon_name, 24);
            m_rbPattern->Append(label, bm.bmp());
        } else {
            m_rbPattern->Append(label);
        }
    };

    add_pattern_item("archimedeanchords", _L("Archimedean Chords"));
    add_pattern_item("monotonic", _L("Monotonic"));
    m_rbPattern->SetSelection(0); // Default to Archimedean Chords
    // ORCA: explicit set value to ensure display on Windows
    m_rbPattern->SetValue(m_rbPattern->GetString(0));
    m_rbPattern->GetDropDown().SetUseContentWidth(true);

    pattern_sizer->Add(pattern_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    pattern_sizer->Add(m_rbPattern , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(pattern_sizer, 0, wxLEFT, FromDIP(3));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});

    auto bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto wiki_btn = new Button(this, "", "toolbar_wiki", 0, 15);
    auto wiki_url = "https://www.orcaslicer.com/wiki/flow_ratio_calib";
    wiki_btn->SetToolTip(_L("Wiki Guide") + "\n" + wiki_url);
    wiki_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Circle);
    wiki_btn->SetCanFocus(false);
    wiki_btn->Bind(wxEVT_LEFT_DOWN, ([wiki_url](auto& e) {wxLaunchDefaultBrowser(wiki_url);}));

    bottom_sizer->Add(wiki_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    bottom_sizer->AddStretchSpacer();
    bottom_sizer->Add(dlg_btns, 0, wxEXPAND);
    v_sizer->Add(bottom_sizer, 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &FlowRateCalibrationDialog::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
    v_sizer->SetSizeHints(this);
}

FlowRateCalibrationDialog::~FlowRateCalibrationDialog() {
    // Disconnect Events
}

void FlowRateCalibrationDialog::on_start(wxCommandEvent& event) {
    int type = m_rbType->GetSelection();
    int patternIdx = m_rbPattern->GetSelection();
    
    InfillPattern pattern = ipArchimedeanChords;
    if (patternIdx == 1) pattern = ipMonotonic;
    
    bool is_linear = (type >= 2);
    int pass = (type % 2) + 1;

    m_plater->calib_flowrate(is_linear, pass, pattern, m_filament->filament_id(), m_filament->extruder_id());
    EndModal(wxID_OK);
}

void FlowRateCalibrationDialog::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
}

namespace {

struct BatchCalibrationTemperatureRow {
    int extruder;
    int start_temperature;
    int end_temperature;
    std::string filament_type;
};

static int batch_filament_type_selection(const std::string& filament_type)
{
    if (filament_type == "PLA")
        return tPLA;
    if (filament_type == "ABS" || filament_type == "ASA")
        return tABS_ASA;
    if (filament_type == "PETG")
        return tPETG;
    if (filament_type == "PCTG")
        return tPCTG;
    if (filament_type == "TPU")
        return tTPU;
    if (filament_type == "PA-CF")
        return tPA_CF;
    if (filament_type == "PET-CF")
        return tPET_CF;
    return tCustom;
}

static std::string batch_filament_type_name(int filament_type)
{
    switch (filament_type) {
    case tPLA: return "PLA";
    case tABS_ASA: return "ABS";
    case tPETG: return "PETG";
    case tPCTG: return "PCTG";
    case tTPU: return "TPU";
    case tPA_CF: return "PA-CF";
    case tPET_CF: return "PET-CF";
    default: return std::string();
    }
}

class BatchCalibrationSettingsDialog : public DPIDialog {
public:
    BatchCalibrationSettingsDialog(wxWindow* parent, wxWindowID id, Plater* plater,
                                   const std::vector<int>& selected_extruders, bool temperature_tower,
                                   const std::vector<CalibrationNozzle>& nozzle_assignments,
                                   const std::vector<wxString>& selected_filament_names,
                                   const std::vector<std::string>& selected_filament_colours,
                                   const std::vector<BatchCalibrationTemperatureRow>& temperature_rows);
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    struct BatchCalibrationTemperatureInput {
        int extruder;
        TextInput* start_input;
        TextInput* end_input;
    };

    void on_start(wxCommandEvent& event);

    RadioGroup* m_rbType;
    ComboBox* m_rbPattern;
    std::vector<BatchCalibrationTemperatureInput> m_temperature_inputs;
    std::vector<int> m_selected_extruders;
    std::vector<wxString> m_selected_filament_names;
    std::vector<std::string> m_selected_filament_colours;
    std::vector<CalibrationNozzle> m_nozzle_assignments;
    bool m_temperature_tower;
    Plater* m_plater;
};

BatchCalibrationSettingsDialog::BatchCalibrationSettingsDialog(
    wxWindow* parent, wxWindowID id, Plater* plater, const std::vector<int>& selected_extruders,
    bool temperature_tower, const std::vector<CalibrationNozzle>& nozzle_assignments,
    const std::vector<wxString>& selected_filament_names,
    const std::vector<std::string>& selected_filament_colours,
    const std::vector<BatchCalibrationTemperatureRow>& temperature_rows)
    : DPIDialog(parent, id, temperature_tower ? _L("Batch Temperature Tower Calibration") : _L("Batch Flow Ratio Calibration"),
                wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_selected_extruders(selected_extruders),
    m_selected_filament_names(selected_filament_names),
      m_selected_filament_colours(selected_filament_colours),
      m_nozzle_assignments(nozzle_assignments),
      m_temperature_tower(temperature_tower), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE);
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    auto v_sizer = new wxBoxSizer(wxVERTICAL);

    auto selected_filaments_box = new LabeledStaticBox(this, _L("Selected filaments"));
    auto selected_filaments_sizer = new wxStaticBoxSizer(selected_filaments_box, wxVERTICAL);
    selected_filaments_sizer->AddSpacer(FromDIP(10));
    auto selected_filament_rows = new wxBoxSizer(wxVERTICAL);
    for (size_t filament_index = 0; filament_index < m_selected_filament_names.size(); ++filament_index) {
        const std::string filament_colour = filament_index < m_selected_filament_colours.size() ?
                                              m_selected_filament_colours[filament_index] :
                                              std::string();
        auto filament_row = new wxBoxSizer(wxHORIZONTAL);
        filament_row->Add(create_filament_color_swatch(selected_filaments_box, filament_colour), 0, wxALIGN_CENTER_VERTICAL);
        auto filament_name = new Label(selected_filaments_box, m_selected_filament_names[filament_index], LB_AUTO_WRAP);
        filament_name->SetMinSize(wxSize(FromDIP(320), -1));
        filament_row->Add(filament_name, 1, wxLEFT | wxALIGN_CENTER_VERTICAL | wxEXPAND, FromDIP(6));
        selected_filament_rows->Add(filament_row, filament_index == 0 ? 0 : wxTOP, FromDIP(3));
    }
    selected_filaments_sizer->Add(selected_filament_rows, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    v_sizer->Add(selected_filaments_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    RadioGroup* filament_type_selector = nullptr;
    if (m_temperature_tower) {
        auto filament_type_box = new LabeledStaticBox(this, _L("Filament type"));
        auto filament_type_sizer = new wxStaticBoxSizer(filament_type_box, wxVERTICAL);
        filament_type_sizer->AddSpacer(FromDIP(10));
        filament_type_selector = new RadioGroup(filament_type_box, { _L("PLA"), _L("ABS/ASA"), _L("PETG"), _L("PCTG"), _L("TPU"), _L("PA-CF"), _L("PET-CF"), _L("Custom") }, wxVERTICAL, 2);
        const bool same_filament_type = !temperature_rows.empty() && std::all_of(temperature_rows.begin(), temperature_rows.end(), [&temperature_rows](const BatchCalibrationTemperatureRow& row) {
            return row.filament_type == temperature_rows.front().filament_type;
        });
        filament_type_selector->SetSelection(batch_filament_type_selection(same_filament_type ? temperature_rows.front().filament_type : std::string()));
        filament_type_sizer->Add(filament_type_selector, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(4));
        v_sizer->Add(filament_type_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    }

    if (!m_temperature_tower) {
        auto type_box = new LabeledStaticBox(this, _L("Flow Ratio variant"));
        auto type_sizer = new wxStaticBoxSizer(type_box, wxVERTICAL);
        m_rbType = new RadioGroup(this, { _L("Pass 1"), _L("Pass 2"), _L("YOLO Linear"), _L("YOLO Fine") }, wxVERTICAL, 1);
        m_rbType->SetSelection(0);
        type_sizer->Add(m_rbType, 0, wxALL, FromDIP(4));
        v_sizer->Add(type_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    }

    auto settings_box = new LabeledStaticBox(this, _L("Settings"));
    auto settings_sizer = new wxStaticBoxSizer(settings_box, wxVERTICAL);
    if (!m_temperature_tower) {
        auto pattern_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto pattern_text = new Label(this, _L("Top Surface Pattern"), wxALIGN_LEFT);
        m_rbPattern = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
        m_rbPattern->Append(_L("Archimedean Chords"));
        m_rbPattern->Append(_L("Monotonic"));
        m_rbPattern->SetSelection(0);
        const int pattern_width = std::max(m_rbPattern->GetTextExtent(_L("Archimedean Chords")).x,
                                           m_rbPattern->GetTextExtent(_L("Monotonic")).x) + FromDIP(48);
        m_rbPattern->SetMinSize(wxSize(pattern_width, -1));
        pattern_sizer->Add(pattern_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
        pattern_sizer->Add(m_rbPattern, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
        settings_sizer->Add(pattern_sizer, 0, wxEXPAND | wxALL, FromDIP(10));
    } else {
        auto temperature_grid = new wxFlexGridSizer(3, FromDIP(0), FromDIP(4));
        auto filament_header = new Label(this, _L("Filament"), wxALIGN_LEFT);
        auto start_header = new Label(this, _L("Start"), wxALIGN_LEFT);
        auto end_header = new Label(this, _L("End"), wxALIGN_LEFT);
        temperature_grid->Add(filament_header, 0, wxALIGN_LEFT);
        temperature_grid->Add(start_header, 0, wxALIGN_LEFT);
        temperature_grid->Add(end_header, 0, wxALIGN_LEFT);
        settings_sizer->Add(temperature_grid, 0, wxEXPAND | wxALL, FromDIP(10));

        for (const BatchCalibrationTemperatureRow& row : temperature_rows) {
            auto filament_text = new Label(this, wxString::Format(_L("Filament %d"), row.extruder + 1), wxALIGN_LEFT);
            const wxString start_value = std::to_string(row.start_temperature);
            const wxString end_value = std::to_string(row.end_temperature);
            const int input_width = std::max(GetTextExtent(start_value).x, GetTextExtent(end_value).x) +
                                      GetTextExtent(_L("℃")).x + FromDIP(20);
            auto start_input = new TextInput(this, start_value, _L("℃"), "", wxDefaultPosition, wxSize(input_width, -1));
            auto end_input = new TextInput(this, end_value, _L("℃"), "", wxDefaultPosition, wxSize(input_width, -1));
            start_input->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
            end_input->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
            temperature_grid->Add(filament_text, 0, wxALIGN_CENTER_VERTICAL);
            temperature_grid->Add(start_input, 0, wxALIGN_CENTER_VERTICAL);
            temperature_grid->Add(end_input, 0, wxALIGN_CENTER_VERTICAL);
            m_temperature_inputs.push_back(BatchCalibrationTemperatureInput{row.extruder, start_input, end_input});
        }

        auto temp_step_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto temp_step_text = new wxStaticText(this, wxID_ANY, _L("Temp step: "), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
        auto temp_step_input = new TextInput(this, wxString::FromDouble(5), _L("\u2103"), "", wxDefaultPosition, wxSize(FromDIP(120), -1));
        temp_step_input->Enable(false);
        temp_step_sizer->Add(temp_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
        temp_step_sizer->Add(temp_step_input, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
        settings_sizer->Add(temp_step_sizer, 0, wxLEFT, FromDIP(3));

        filament_type_selector->Bind(wxEVT_COMMAND_RADIOBOX_SELECTED, [this](wxCommandEvent& event) {
            const auto [start_temperature, end_temperature] = batch_temperature_tower_defaults(batch_filament_type_name(event.GetSelection()));
            for (auto& [extruder, start_input, end_input] : m_temperature_inputs) {
                start_input->GetTextCtrl()->SetValue(std::to_string(start_temperature));
                end_input->GetTextCtrl()->SetValue(std::to_string(end_temperature));
            }
            event.Skip();
        });
    }
    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    auto sequence_note = new Label(this, _L("Generated batches print one filament time using By Object. "
                                            "Changing the project to By Layer removes that isolation; "
                                            "physical print order is not tied to selection or layout order."),
                                   LB_AUTO_WRAP, wxSize(FromDIP(360), -1));
    sequence_note->SetToolTip(_L("By Object is required for one-filament-at-a-time calibration isolation."));
    v_sizer->Add(sequence_note, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    auto dlg_btns = new DialogButtons(this, {"OK"});
    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &BatchCalibrationSettingsDialog::on_start, this);
    v_sizer->AddSpacer(FromDIP(8));
    v_sizer->Add(dlg_btns, 0, wxEXPAND);

    SetSizerAndFit(v_sizer);
    SetSize(wxSize(FromDIP(560), -1));
    wxGetApp().UpdateDlgDarkUI(this);
}

void BatchCalibrationSettingsDialog::on_start(wxCommandEvent& event) {
    if (m_temperature_tower) {
        std::vector<Calib_Params> tower_params;
        tower_params.reserve(m_temperature_inputs.size());
        for (const auto& [extruder, start_input, end_input] : m_temperature_inputs) {
            long start_temperature = 0;
            long end_temperature = 0;
            start_input->GetTextCtrl()->GetValue().ToLong(&start_temperature);
            end_input->GetTextCtrl()->GetValue().ToLong(&end_temperature);

            Calib_Params params;
            params.mode = CalibMode::Calib_Temp_Tower;
            params.extruder_id = extruder;
            params.start = start_temperature;
            params.end = end_temperature;
            params.nozzle_based_resize = true;
            tower_params.push_back(params);
        }

        wxString error_message;
        if (!m_plater->calib_batch_temperature(tower_params, error_message)) {
            if (!error_message.empty()) {
                MessageDialog msg_dlg(this, error_message, wxEmptyString, wxICON_WARNING | wxOK);
                msg_dlg.ShowModal();
            }
            return;
        }
    } else {
        int type = m_rbType->GetSelection();
        InfillPattern pattern = ipArchimedeanChords;
        if (m_rbPattern->GetSelection() == 1)
            pattern = ipMonotonic;
        bool is_linear = (type >= 2);
        int pass = (type % 2) + 1;

        wxString error_message;
        if (!m_plater->calib_batch_flowrate(
                m_selected_extruders, m_nozzle_assignments, is_linear, pass, pattern, error_message)) {
            if (!error_message.empty()) {
                MessageDialog msg_dlg(this, error_message, wxEmptyString, wxICON_WARNING | wxOK);
                msg_dlg.ShowModal();
            }
            return;
        }
    }

    EndModal(wxID_OK);
    event.Skip();
}

void BatchCalibrationSettingsDialog::on_dpi_changed(const wxRect& suggested_rect) {
    if (GetSizer() != nullptr) {
        GetSizer()->SetSizeHints(this);
        Fit();
    }
    this->Refresh();
}

}

BatchCalibrationDialog::BatchCalibrationDialog(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Batch Calibration"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_plater(plater)
{
    SetBackgroundColour(*wxWHITE);
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    auto v_sizer = new wxBoxSizer(wxVERTICAL);

    auto filament_box = new LabeledStaticBox(this, _L("Loaded filaments"));
    auto filament_sizer = new wxStaticBoxSizer(filament_box, wxVERTICAL);
    filament_sizer->AddSpacer(FromDIP(10));
    const auto* preset_bundle = wxGetApp().preset_bundle;
    const auto& project_config = preset_bundle->project_config;
    const auto full_config = preset_bundle->full_config();
    const auto* flow_ratios = full_config.option<ConfigOptionFloatsNullable>("filament_flow_ratio");
    const auto* nozzle_diameters = full_config.option<ConfigOptionFloats>("nozzle_diameter");
    const auto* extruder_printable_areas = full_config.option<ConfigOptionPointsGroups>("extruder_printable_area");
    const bool dual_nozzle = nozzle_diameters != nullptr && nozzle_diameters->size() == 2 &&
        extruder_printable_areas != nullptr && extruder_printable_areas->size() >= 2;
    const size_t filament_count = flow_ratios == nullptr ? 0 :
        std::min(preset_bundle->filament_presets.size(), flow_ratios->values.size());

    auto filament_grid = new wxFlexGridSizer(dual_nozzle ? 3 : 2, FromDIP(0), FromDIP(3));
    filament_grid->AddGrowableCol(1, 1);
    for (size_t extruder = 0; extruder < filament_count; ++extruder) {
        const double flow_ratio = flow_ratios->get_at(static_cast<int>(extruder));
        wxString label = wxString::Format(_L("Filament %d"), static_cast<int>(extruder + 1));
        const Preset* preset = preset_bundle->filaments.find_preset(preset_bundle->filament_presets[extruder], false);
        const bool flow_usable = preset != nullptr && std::isfinite(flow_ratio) && flow_ratio > 0.0;
        std::string filament_type;
        bool temperature_usable = false;
        if (preset != nullptr) {
            if (const auto* filament_types = preset->config.option<ConfigOptionStrings>("filament_type"); filament_types != nullptr)
                filament_type = filament_types->get_at(0);
            if (const auto* nozzle_temperatures = preset->config.option<ConfigOptionInts>("nozzle_temperature"); nozzle_temperatures != nullptr)
                temperature_usable = nozzle_temperatures->get_at(0) > 0;
            label += " - " + wxString::FromUTF8(preset->name);
        }
        if (flow_usable)
            label += wxString::Format(_L(" (Flow ratio: %.3f)"), flow_ratio);
        const auto [start_temperature, end_temperature] = batch_temperature_tower_defaults(filament_type);
        std::string filament_colour;
        if (const auto* filament_colour_option = project_config.option<ConfigOptionStrings>("filament_colour");
            filament_colour_option != nullptr && extruder < filament_colour_option->values.size())
            filament_colour = filament_colour_option->get_at(static_cast<unsigned int>(extruder));
        if (filament_colour.empty() && preset != nullptr) {
            if (const auto* default_filament_colour_option = preset->config.option<ConfigOptionStrings>("default_filament_colour");
                default_filament_colour_option != nullptr && !default_filament_colour_option->values.empty())
                filament_colour = default_filament_colour_option->get_at(0);
        }

        auto checkbox = new ::CheckBox(filament_box);
        ComboBox* nozzle_choice = nullptr;
        if (dual_nozzle) {
            nozzle_choice = new ComboBox(
                filament_box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr,
                wxCB_READONLY);
            nozzle_choice->Append(_L("Left nozzle"));
            nozzle_choice->Append(_L("Right nozzle"));
            nozzle_choice->SetSelection(0);
            nozzle_choice->SetToolTip(_L("Used only for calibration placement; it does not assign the filament to a nozzle."));
            const int nozzle_width = std::max(
                nozzle_choice->GetTextExtent(_L("Left nozzle")).x, nozzle_choice->GetTextExtent(_L("Right nozzle")).x) + FromDIP(48);
            nozzle_choice->SetMinSize(wxSize(nozzle_width, -1));
        }
        auto label_text = new Label(filament_box, label, LB_AUTO_WRAP);
        label_text->SetMinSize(wxSize(FromDIP(320), -1));
        label_text->SetCursor(wxCURSOR_HAND);
        label_text->Bind(wxEVT_LEFT_DOWN, [checkbox](wxMouseEvent& event) {
            if (checkbox->IsEnabled())
                checkbox->SetValue(!checkbox->GetValue());
            event.Skip();
        });
        auto label_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto filament_colour_swatch = create_filament_color_swatch(filament_box, filament_colour);
        filament_colour_swatch->SetCursor(wxCURSOR_HAND);
        filament_colour_swatch->Bind(wxEVT_LEFT_DOWN, [checkbox](wxMouseEvent& event) {
            if (checkbox->IsEnabled())
                checkbox->SetValue(!checkbox->GetValue());
            event.Skip();
        });
        label_sizer->Add(filament_colour_swatch, 0, wxALIGN_CENTER_VERTICAL);
        filament_grid->Add(checkbox, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(3));
        label_sizer->Add(label_text, 1, wxLEFT | wxALIGN_CENTER_VERTICAL | wxEXPAND, FromDIP(6));
        filament_grid->Add(label_sizer, 0, wxLEFT | wxEXPAND, FromDIP(8));
        if (dual_nozzle)
            filament_grid->Add(nozzle_choice, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(8));
        m_filament_rows.push_back({
            checkbox,
            nozzle_choice,
            static_cast<int>(extruder),
            preset ? wxString::FromUTF8(preset->name) : wxString::Format(_L("Filament %d"), static_cast<int>(extruder + 1)),
            filament_colour,
        filament_type,
            start_temperature,
            end_temperature,
            flow_usable,
            temperature_usable,
        });
    }
    filament_sizer->Add(filament_grid, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    v_sizer->Add(filament_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    auto test_type_box = new LabeledStaticBox(this, _L("Test type"));
    auto test_type_sizer = new wxStaticBoxSizer(test_type_box, wxVERTICAL);
    test_type_sizer->AddSpacer(FromDIP(10));
    m_rbTestType = new RadioGroup(test_type_box, { _L("Flow Ratio"), _L("Temperature Tower") }, wxVERTICAL, 1);
    m_rbTestType->SetSelection(0);
    test_type_sizer->Add(m_rbTestType, 0, wxALL, FromDIP(4));
    v_sizer->Add(test_type_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    auto dlg_btns = new DialogButtons(this, {"Next"});
    dlg_btns->GetNEXT()->Bind(wxEVT_BUTTON, &BatchCalibrationDialog::on_next, this);
    v_sizer->AddSpacer(FromDIP(8));
    v_sizer->Add(dlg_btns, 0, wxEXPAND);

    SetSizerAndFit(v_sizer);
    SetSize(wxSize(FromDIP(560), -1));
    m_rbTestType->Bind(wxEVT_COMMAND_RADIOBOX_SELECTED, [this](wxCommandEvent& event) {
        on_test_type_changed(event);
    });
    update_filament_availability();
    wxGetApp().UpdateDlgDarkUI(this);
}

void BatchCalibrationDialog::on_next(wxCommandEvent& event)
{
    std::vector<std::pair<int, CalibrationNozzle>> selected_filaments;
    for (const BatchFilamentRow& row : m_filament_rows) {
        if (row.check->GetValue())
            selected_filaments.emplace_back(
                row.extruder,
                row.nozzle_choice == nullptr || row.nozzle_choice->GetSelection() == 0 ?
                    CalibrationNozzle::Left : CalibrationNozzle::Right);
    }
    std::sort(selected_filaments.begin(), selected_filaments.end());

    std::vector<int> selected_extruders;
    std::vector<CalibrationNozzle> nozzle_assignments;
    std::vector<wxString> selected_filament_names;
    std::vector<std::string> selected_filament_colours;
    selected_extruders.reserve(selected_filaments.size());
    nozzle_assignments.reserve(selected_filaments.size());
    selected_filament_names.reserve(selected_filaments.size());
    selected_filament_colours.reserve(selected_filaments.size());
    for (const auto& [extruder, nozzle] : selected_filaments) {
        selected_extruders.push_back(extruder);
        nozzle_assignments.push_back(nozzle);
        for (const BatchFilamentRow& row : m_filament_rows) {
            if (row.extruder == extruder) {
                selected_filament_names.push_back(row.filament_name);
                selected_filament_colours.push_back(row.filament_colour);
                break;
            }
        }
    }

    if (selected_extruders.empty()) {
        MessageDialog msg_dlg(this, _L("Please select at least one filament."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    std::vector<BatchCalibrationTemperatureRow> temperature_rows;
    temperature_rows.reserve(selected_extruders.size());
    if (m_rbTestType->GetSelection() == 1) {
        for (const int selected_extruder : selected_extruders) {
            for (const BatchFilamentRow& row : m_filament_rows) {
                if (row.extruder == selected_extruder) {
                    temperature_rows.push_back({ row.extruder, row.start_temperature, row.end_temperature, row.filament_type });
                    break;
                }
            }
        }
    }

    BatchCalibrationSettingsDialog settings_dlg(this, wxID_ANY, m_plater, selected_extruders,
                                                m_rbTestType->GetSelection() == 1, nozzle_assignments, selected_filament_names,
                                                selected_filament_colours, temperature_rows);
    if (settings_dlg.ShowModal() == wxID_OK)
        EndModal(wxID_OK);
    event.Skip();
}

void BatchCalibrationDialog::on_test_type_changed(wxCommandEvent& event)
{
    update_filament_availability();
    event.Skip();
}

void BatchCalibrationDialog::update_filament_availability()
{
    const bool temperature_tower = m_rbTestType->GetSelection() == 1;
    for (BatchFilamentRow& row : m_filament_rows) {
        const bool usable = temperature_tower ? row.temperature_usable : row.flow_usable;
        row.check->Enable(usable);
        if (row.nozzle_choice != nullptr)
            row.nozzle_choice->Enable(usable);
        if (!usable)
            row.check->SetValue(false);
        if (!usable)
            row.check->SetToolTip(temperature_tower ?
                _L("This filament has no usable Temperature Tower settings.") :
                _L("This filament has no usable Flow ratio."));
        else
            row.check->UnsetToolTip();
    }
}

void BatchCalibrationDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    Layout();
    Fit();
    this->Refresh();
}

}} // namespace Slic3r::GUI

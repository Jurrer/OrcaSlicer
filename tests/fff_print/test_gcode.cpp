#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <catch2/catch_message.hpp>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/calib.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

#include "test_helpers.hpp"

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Point.hpp"
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include <map>

using namespace Slic3r;

namespace {

std::vector<double> pressure_advance_tower_values(const std::string& gcode)
{
    std::vector<double> values;
    std::istringstream stream(gcode);
    for (std::string line; std::getline(stream, line);) {
        if (line.rfind("M900 K", 0) == 0)
            values.push_back(std::stod(line.substr(6)));
    }
    return values;
}

std::vector<double> temperature_tower_values(const std::string& gcode)
{
    std::vector<double> values;
    std::istringstream stream(gcode);
    bool in_layer_gcode = false;
    for (std::string line; std::getline(stream, line);) {
        if (line.rfind(";Z:", 0) == 0)
            in_layer_gcode = true;
        else if (line.rfind("; stop printing object", 0) == 0 || line.rfind(";TYPE:Custom", 0) == 0)
            in_layer_gcode = false;
        if (in_layer_gcode && line.rfind("M104 S", 0) == 0)
            values.push_back(std::stod(line.substr(6)));
    }
    return values;
}

std::map<std::string, double> extrusion_totals_by_object(const std::string& gcode)
{
    std::map<std::string, double> totals;
    std::string current_object;
    bool in_outer_wall = false;
    GCodeReader reader;
    reader.parse_buffer(gcode, [&](GCodeReader& self, GCodeReader::GCodeLine& line) {
        const std::string comment(line.raw());
        constexpr std::string_view object_prefix = "; printing object ";
        if (comment.rfind(";TYPE:", 0) == 0)
            in_outer_wall = comment.find("Outer wall") != std::string::npos;
        if (comment.rfind(object_prefix) == 0) {
            const size_t id_position = comment.find(" id:");
            current_object = id_position == std::string::npos ? comment.substr(object_prefix.size()) :
                comment.substr(object_prefix.size(), id_position - object_prefix.size());
        } else if (comment.rfind("; stop printing object") == 0) {
            current_object.clear();
        } else if (line.extruding(self) && !current_object.empty() && in_outer_wall) {
            totals[current_object] += line.dist_E(self);
        }
    });
    return totals;
}

}

TEST_CASE("Klipper object labels name each copy without the characters Klipper cannot parse", "[GCode]")
{
    const auto [name, label] = GENERATE(table<std::string, std::string>({
        {"my part (2)", "my_part_2"},
        {"(cube)", "cube"},
    }));
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({{"gcode_flavor", "klipper"}, {"exclude_object", "1"}});
    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(20.)}, print, model, config, nullptr, false, 2);
    model.objects.front()->name = name;
    arrange_objects(model, BoundingBox{Point::new_scale(0., 0.), Point::new_scale(500., 500.)},
                    ArrangeParams{scaled(min_object_distance(config))});
    print.apply(model, config);

    const std::string gcode = Test::gcode(print);
    for (const char *copy : {"0", "1"}) {
        const std::string instance_label = label + "_id_0_copy_" + copy;
        INFO(instance_label);
        CHECK(gcode.find("EXCLUDE_OBJECT_DEFINE NAME=" + instance_label + " ") != std::string::npos);
        CHECK(gcode.find("EXCLUDE_OBJECT_START NAME=" + instance_label + "\n") != std::string::npos);
    }
}

TEST_CASE("Calibration parameters resolve per object and fall back to the print", "[GCode][Calibration]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"print_sequence", "by object"},
        {"skirt_loops", "0"},
        {"brim_type", "no_brim"},
        {"layer_height", "0.2"},
        {"initial_layer_print_height", "0.2"},
    });

    Print print;
    Model model;
    const std::vector<std::vector<Slic3r::ConfigBase::SetDeserializeItem>> object_overrides = {
        {{"extruder", "1"}, {"print_flow_ratio", std::to_string(0.97 / 0.95)}},
        {{"extruder", "2"}, {"print_flow_ratio", std::to_string(1.08 / 1.10)}},
    };
    Test::init_print(std::vector<TriangleMesh>{Test::cube(20.), Test::cube(20.)}, print, model, config, &object_overrides);

    Calib_Params global_params;
    global_params.mode = CalibMode::Calib_PA_Tower;
    global_params.start = 0.02;
    global_params.end = 0.04;
    global_params.step = 0.01;
    print.set_calib_params(global_params);

    Calib_Params object_params = global_params;
    object_params.start = 0.03;
    object_params.end = 0.05;
    print.set_object_calib_params(*model.objects.front(), object_params);

    const std::vector<double> values = pressure_advance_tower_values(Test::gcode(print));
    REQUIRE(values.size() == 200);
    REQUIRE_THAT(values.front(), Catch::Matchers::WithinAbs(0.03, 1e-6));
    REQUIRE_THAT(values[99], Catch::Matchers::WithinAbs(0.04, 1e-6));
    REQUIRE_THAT(values[100], Catch::Matchers::WithinAbs(0.03, 1e-6));
    REQUIRE_THAT(values.back(), Catch::Matchers::WithinAbs(0.04, 1e-6));
}

TEST_CASE("Prints without object calibration parameters use the global calibration values", "[GCode][Calibration]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"print_sequence", "by object"},
        {"skirt_loops", "0"},
        {"brim_type", "no_brim"},
        {"layer_height", "0.2"},
        {"initial_layer_print_height", "0.2"},
    });

    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(20.)}, print, model, config);

    Calib_Params params;
    params.mode = CalibMode::Calib_PA_Tower;
    params.start = 0.02;
    params.end = 0.04;
    params.step = 0.01;
    print.set_calib_params(params);

    const std::vector<double> values = pressure_advance_tower_values(Test::gcode(print));
    REQUIRE(values.size() == 100);
    REQUIRE_THAT(values.front(), Catch::Matchers::WithinAbs(0.02, 1e-6));
    REQUIRE_THAT(values[99], Catch::Matchers::WithinAbs(0.04, 1e-6));
    CHECK_THAT(print.calib_params(*model.objects.front()).start, Catch::Matchers::WithinAbs(0.02, 1e-6));
}

TEST_CASE("Temperature tower ramps use each object's layer progression", "[GCode][Calibration]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"print_sequence", "by object"},
        {"skirt_loops", "0"},
        {"brim_type", "no_brim"},
        {"layer_height", "0.2"},
        {"initial_layer_print_height", "0.2"},
    });

    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(20.), Test::cube(20.)}, print, model, config);

    Calib_Params first_params;
    first_params.mode = CalibMode::Calib_Temp_Tower;
    first_params.start = 200;
    first_params.end = 210;
    print.set_object_calib_params(*model.objects[0], first_params);

    Calib_Params second_params;
    second_params.mode = CalibMode::Calib_Temp_Tower;
    second_params.start = 220;
    second_params.end = 230;
    print.set_object_calib_params(*model.objects[1], second_params);

    const std::vector<double> values = temperature_tower_values(Test::gcode(print));
    REQUIRE(values.size() == 200);
    REQUIRE_THAT(values.front(), Catch::Matchers::WithinAbs(200, 1e-6));
    REQUIRE_THAT(values[99], Catch::Matchers::WithinAbs(210, 1e-6));
    REQUIRE_THAT(values[100], Catch::Matchers::WithinAbs(220, 1e-6));
    REQUIRE_THAT(values.back(), Catch::Matchers::WithinAbs(230, 1e-6));
}

TEST_CASE("Temperature tower without object parameters keeps its print-wide ramp", "[GCode][Calibration]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"print_sequence", "by object"},
        {"skirt_loops", "0"},
        {"brim_type", "no_brim"},
        {"layer_height", "0.2"},
        {"initial_layer_print_height", "0.2"},
    });

    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(20.)}, print, model, config);

    Calib_Params params;
    params.mode = CalibMode::Calib_Temp_Tower;
    params.start = 200;
    params.end = 210;
    print.set_calib_params(params);

    const std::vector<double> values = temperature_tower_values(Test::gcode(print));
    REQUIRE(values.size() == 100);
    REQUIRE_THAT(values.front(), Catch::Matchers::WithinAbs(200, 1e-6));
    REQUIRE_THAT(values.back(), Catch::Matchers::WithinAbs(210, 1e-6));
}

TEST_CASE("Batch temperature tower defaults follow the single-tower material defaults", "[GCode][Calibration]")
{
    CHECK(batch_temperature_tower_defaults("PLA").first == 230);
    CHECK(batch_temperature_tower_defaults("PLA").second == 190);
    CHECK(batch_temperature_tower_defaults("ABS").first == 270);
    CHECK(batch_temperature_tower_defaults("ASA").second == 230);
    CHECK(batch_temperature_tower_defaults("PETG").first == 250);
    CHECK(batch_temperature_tower_defaults("PCTG").second == 240);
    CHECK(batch_temperature_tower_defaults("TPU").first == 240);
    CHECK(batch_temperature_tower_defaults("PA-CF").first == 320);
    CHECK(batch_temperature_tower_defaults("PET-CF").second == 280);
    CHECK(batch_temperature_tower_defaults("Custom").first == 230);
    CHECK(batch_temperature_tower_defaults("Custom").second == 190);
}

TEST_CASE("Batch temperature tower validation rejects invalid rows and mismatched nozzles", "[GCode][Calibration]")
{
    CHECK(valid_batch_temperature_tower(230, 190));
    CHECK_FALSE(valid_batch_temperature_tower(505, 190));
    CHECK_FALSE(valid_batch_temperature_tower(230, 150));
    CHECK(valid_batch_temperature_tower(230, 225));
    CHECK_FALSE(valid_batch_temperature_tower(230, 230));

    CHECK(batch_nozzle_diameters_match({0.4, 0.4}));
    CHECK_FALSE(batch_nozzle_diameters_match({}));
    CHECK_FALSE(batch_nozzle_diameters_match({0.4, 0.0}));
    CHECK_FALSE(batch_nozzle_diameters_match({0.4, 0.6}));
}

TEST_CASE("Batch temperature tower ramps use each selected filament", "[GCode][Calibration]")
{
    DynamicPrintConfig config = Test::multifilament_config(2);
    config.set_deserialize_strict({
        {"nozzle_diameter", "0.4,0.4"},
        {"printer_extruder_id", "1,2"},
        {"printer_extruder_variant", "Direct Drive Standard,Direct Drive Standard"},
        {"extruder_printable_height", "0,0"},
        {"single_extruder_multi_material", "0"},
        {"print_sequence", "by object"},
        {"skirt_loops", "0"},
        {"brim_type", "no_brim"},
        {"layer_height", "0.2"},
        {"initial_layer_print_height", "0.2"},
    });

    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(20.), Test::cube(20.)}, print, model, config);
    model.objects[0]->name = "Batch Temperature Tower E1";
    model.objects[0]->config.set_key_value("extruder", new ConfigOptionInt(1));
    model.objects[1]->name = "Batch Temperature Tower E2";
    model.objects[1]->config.set_key_value("extruder", new ConfigOptionInt(2));

    Calib_Params global_params;
    global_params.mode = CalibMode::Calib_Temp_Tower;
    print.set_calib_params(global_params);
    Calib_Params first_params = global_params;
    first_params.extruder_id = 0;
    first_params.start = 200;
    first_params.end = 210;
    print.set_object_calib_params(*model.objects[0], first_params);
    Calib_Params second_params = global_params;
    second_params.extruder_id = 1;
    second_params.start = 220;
    second_params.end = 230;
    print.set_object_calib_params(*model.objects[1], second_params);

    const std::vector<double> values = temperature_tower_values(Test::gcode(print));
    REQUIRE(values.size() == 200);
    REQUIRE_THAT(values.front(), Catch::Matchers::WithinAbs(200, 1e-6));
    REQUIRE_THAT(values[99], Catch::Matchers::WithinAbs(210, 1e-6));
    REQUIRE_THAT(values[100], Catch::Matchers::WithinAbs(220, 1e-6));
    REQUIRE_THAT(values.back(), Catch::Matchers::WithinAbs(230, 1e-6));
}

TEST_CASE("ByObject automated filament changes use the existing BBL tool-change command", "[GCode][Calibration]")
{
    DynamicPrintConfig config = Test::multifilament_config(2);
    config.set_deserialize_strict({
        {"nozzle_diameter", "0.4,0.4"},
        {"printer_extruder_id", "1,2"},
        {"printer_extruder_variant", "Direct Drive Standard,Direct Drive Standard"},
        {"single_extruder_multi_material", "0"},
        {"print_sequence", "by object"},
        {"skirt_loops", "0"},
        {"brim_type", "no_brim"},
        {"layer_height", "0.2"},
        {"initial_layer_print_height", "0.2"},
    });

    Print print;
    print.is_BBL_printer() = true;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(20.), Test::cube(20.)}, print, model, config);
    model.objects[0]->config.set_key_value("extruder", new ConfigOptionInt(1));
    model.objects[1]->config.set_key_value("extruder", new ConfigOptionInt(2));
    print.apply(model, config);

    const std::string gcode = Test::gcode(print);
    CHECK(gcode.find("M1020 S1 H") != std::string::npos);
    CHECK(gcode.find(GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Manual_Tool_Change)) == std::string::npos);
}

TEST_CASE("ByObject manual filament changes emit the existing manual marker", "[GCode][Calibration]")
{
    DynamicPrintConfig config = Test::multifilament_config(2);
    config.set_deserialize_strict({
        {"nozzle_diameter", "0.4"},
        {"printer_extruder_id", "1,1"},
        {"single_extruder_multi_material", "1"},
        {"manual_filament_change", "1"},
        {"print_sequence", "by object"},
        {"skirt_loops", "0"},
        {"brim_type", "no_brim"},
        {"layer_height", "0.2"},
        {"initial_layer_print_height", "0.2"},
    });

    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(20.), Test::cube(20.)}, print, model, config);
    model.objects[0]->config.set_key_value("extruder", new ConfigOptionInt(1));
    model.objects[1]->config.set_key_value("extruder", new ConfigOptionInt(2));
    print.apply(model, config);

    const std::string gcode = Test::gcode(print);
    const std::string marker = GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Manual_Tool_Change);
    CHECK(gcode.find(";" + marker + "T1") != std::string::npos);
    CHECK(gcode.find("M1020 S1 H") == std::string::npos);
}

TEST_CASE("Per-filament flow ratio groups apply each filament's current ratio", "[GCode][Calibration]")
{
    DynamicPrintConfig config = Test::multifilament_config(2);
    config.set_deserialize_strict({
        {"nozzle_diameter", "0.4,0.4"},
        {"printer_extruder_id", "1,2"},
        {"printer_extruder_variant", "Direct Drive Standard,Direct Drive Standard"},
        {"extruder_printable_height", "0,0"},
        {"single_extruder_multi_material", "0"},
    });
    config.opt<ConfigOptionFloatsNullable>("filament_flow_ratio")->values = {0.95, 1.10};
    config.opt<ConfigOptionInts>("filament_self_index")->values = {1, 2};
    config.set_deserialize_strict({
        {"print_sequence", "by object"},
        {"skirt_loops", "0"},
        {"brim_type", "no_brim"},
        {"layer_height", "0.2"},
        {"initial_layer_print_height", "0.2"},
    });

    Print print;
    Model model;
    Test::init_print(std::vector<TriangleMesh>{Test::cube(20.), Test::cube(20.)}, print, model, config);

    model.objects[0]->name = "flowrate_0.02";
    model.objects[0]->config.set_key_value("extruder", new ConfigOptionInt(1));
    const auto first_print_ratio = flow_ratio_calibration_print_flow_ratio(*model.objects[0], 0.95, true);
    REQUIRE(first_print_ratio.has_value());
    REQUIRE_THAT(*first_print_ratio, Catch::Matchers::WithinRel(0.97 / 0.95, 1e-6));
    model.objects[0]->config.set_key_value("print_flow_ratio", new ConfigOptionFloat(*first_print_ratio));

    model.objects[1]->name = "flowrate_m0.02";
    model.objects[1]->config.set_key_value("extruder", new ConfigOptionInt(2));
    const auto second_print_ratio = flow_ratio_calibration_print_flow_ratio(*model.objects[1], 1.10, true);
    REQUIRE(second_print_ratio.has_value());
    REQUIRE_THAT(*second_print_ratio, Catch::Matchers::WithinRel(1.08 / 1.10, 1e-6));
    model.objects[1]->config.set_key_value("print_flow_ratio", new ConfigOptionFloat(*second_print_ratio));

    print.apply(model, config);
    const std::string gcode = Test::gcode(print);
    const std::map<std::string, double> totals = extrusion_totals_by_object(gcode);
    REQUIRE(totals.at(model.objects[0]->name) > 0.0);
    REQUIRE(totals.at(model.objects[1]->name) > 0.0);
    REQUIRE_THAT(totals.at(model.objects[1]->name) / totals.at(model.objects[0]->name),
        Catch::Matchers::WithinRel(1.08 / 0.97, 0.02));
    REQUIRE_THAT(print.config().filament_flow_ratio.get_at(0), Catch::Matchers::WithinAbs(0.95, 1e-6));
    REQUIRE_THAT(print.config().filament_flow_ratio.get_at(1), Catch::Matchers::WithinAbs(1.10, 1e-6));
}

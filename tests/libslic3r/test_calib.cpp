#include <catch2/catch_all.hpp>

#include <algorithm>
#include "libslic3r/CustomGCode.hpp"
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <array>
#include <optional>
#include <vector>
#include "libslic3r/calib.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Arrange.hpp"

using namespace Slic3r;

TEST_CASE("Batch calibration maps filament slots to physical nozzles", "[Calib]")
{
    const std::vector<int> filament_map{1, 2, 1, 2};
    REQUIRE(batch_filament_physical_nozzle_index(0, filament_map, 2, true) == 0);
    REQUIRE(batch_filament_physical_nozzle_index(1, filament_map, 2, true) == 1);
    REQUIRE(batch_filament_physical_nozzle_index(2, filament_map, 2, true) == 0);
    REQUIRE(batch_filament_physical_nozzle_index(3, filament_map, 2, true) == 1);
    REQUIRE(batch_filament_physical_nozzle_index(4, {1}, 2, true) == std::nullopt);
    REQUIRE(batch_filament_physical_nozzle_index(1, {3}, 2, true) == std::nullopt);
}

TEST_CASE("A sole BBL nozzle accepts every selected filament slot", "[Calib]")
{
    REQUIRE(batch_filament_physical_nozzle_index(2, {}, 1, true) == 0);
    REQUIRE(batch_filament_physical_nozzle_index(3, {2}, 1, true) == 0);
}

TEST_CASE("Non-BBL filament slots map directly to physical nozzles", "[Calib]")
{
    REQUIRE(batch_filament_physical_nozzle_index(2, {1}, 4, false) == 2);
    REQUIRE(batch_filament_physical_nozzle_index(4, {1}, 4, false) == std::nullopt);
}

TEST_CASE("Invalid temperature tower ranges fail validation", "[Calib]")
{
    REQUIRE_FALSE(valid_batch_temperature_tower(160, 200));
    REQUIRE_FALSE(valid_batch_temperature_tower(300, 300));
    REQUIRE(valid_batch_temperature_tower(300, 200));
}

namespace {

// The width-resolution getters are protected; expose them so the resolution can be asserted directly.
struct PaPatternProbe : public CalibPressureAdvancePattern
{
    using CalibPressureAdvancePattern::CalibPressureAdvancePattern;
    using CalibPressureAdvancePattern::line_width;
    using CalibPressureAdvancePattern::line_width_first_layer;
};

} // namespace

TEST_CASE("Zero calibration line width resolves to a positive default", "[Calib][Regression]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"line_width", "0"},
        {"initial_layer_line_width", "0"},
    });

    Model model;
    model.add_object("cube", "", make_cube(20, 20, 20))->add_instance();

    Calib_Params params;
    params.mode = CalibMode::Calib_PA_Pattern;

    PaPatternProbe pattern(params, config, /* is_bbl_machine */ true, *model.objects.front(), Vec3d(0, 0, 0));

    REQUIRE(pattern.line_width() > 0.);
    REQUIRE(pattern.line_width_first_layer() > 0.);
}

TEST_CASE("Flow ratio members merge into one calibration assembly with member settings", "[Calib]")
{
    Model model;
    std::vector<ModelObject *> members;
    for (const std::string &name : {"flowrate_0.98", "flowrate_1.02"}) {
        ModelObject *member = model.add_object(name.c_str(), "", make_cube(10, 10, 10));
        member->add_instance();
        member->config.set_key_value("print_flow_ratio", new ConfigOptionFloat(0.98f));
        member->config.set_key_value("wall_loops", new ConfigOptionInt(2));
        member->config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTree));
        member->config.set("extruder", 2);
        member->volumes.front()->config.set_key_value("wall_loops", new ConfigOptionInt(3));
        members.push_back(member);
    }

    std::string error;
    ModelObject *assembly = build_flow_ratio_calibration_assembly(model, members, 2, members.size(), error);

    CAPTURE(error);
    REQUIRE(assembly != nullptr);
    REQUIRE(error.empty());
    REQUIRE(model.objects.size() == 1);
    REQUIRE(assembly->name == "Batch Flow Ratio E2");
    REQUIRE(assembly->volumes.size() == 2);
    REQUIRE(assembly->volumes.front()->name == "flowrate_0.98");
    REQUIRE(assembly->volumes.back()->name == "flowrate_1.02");
    REQUIRE_THAT(assembly->volumes.front()->config.opt_float("print_flow_ratio"),
                 Catch::Matchers::WithinAbs(0.98, 1e-7));
    REQUIRE(assembly->volumes.front()->config.opt_int("wall_loops") == 2);
    REQUIRE(assembly->volumes.back()->config.opt_int("wall_loops") == 2);
    REQUIRE(assembly->volumes.front()->config.opt_int("extruder") == 2);
    REQUIRE(assembly->config.opt_serialize("support_type") == "tree(manual)");
}

TEST_CASE("Flow ratio assemblies validate member count, names, and values", "[Calib]")
{
    Model model;
    ModelObject* member = model.add_object("flowrate_0", "", make_cube(10, 10, 10));
    member->add_instance();

    std::vector<ModelObject*> members{member};
    std::string error;
    ModelObject* incomplete_assembly = build_flow_ratio_calibration_assembly(
        model, members, 2, 2, error);
    REQUIRE(incomplete_assembly == nullptr);
    REQUIRE(error == "The Flow Ratio calibration model contains 1 test members; expected 2.");
    REQUIRE(model.objects.size() == 1);

    member->name = "flowrate_invalid";
    ModelObject* malformed_name_assembly = build_flow_ratio_calibration_assembly(
        model, members, 2, 1, error);
    REQUIRE(malformed_name_assembly == nullptr);
    REQUIRE(error == "The Flow Ratio calibration model contains a malformed test member name: flowrate_invalid");

    member->name = "flowrate_1.0extra";
    ModelObject* malformed_value_assembly = build_flow_ratio_calibration_assembly(
        model, members, 2, 1, error);
    REQUIRE(malformed_value_assembly == nullptr);
    REQUIRE(error == "The Flow Ratio calibration model contains a malformed test member name: flowrate_1.0extra");
}

TEST_CASE("Flow ratio members reject additional printable volumes", "[Calib]")
{
    Model model;
    ModelObject *member = model.add_object("flowrate_0.98", "", make_cube(10, 10, 10));
    member->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    member->add_instance();

    std::string error;
    std::vector<ModelObject*> members{member};
    ModelObject *assembly = build_flow_ratio_calibration_assembly(model, members, 2, 1, error);

    REQUIRE(assembly == nullptr);
    REQUIRE(error == "The Flow Ratio calibration member flowrate_0.98 must have one printable volume.");
}

TEST_CASE("Flow ratio extruder assignment is model metadata rather than a region setting", "[Calib]")
{
    Model model;
    ModelObject *member = model.add_object("flowrate_0.98", "", make_cube(10, 10, 10));
    member->add_instance();
    member->config.set("extruder", 2);

    std::string error;
    std::vector<ModelObject*> members{member};
    ModelObject *assembly = build_flow_ratio_calibration_assembly(model, members, 2, 1, error);

    CAPTURE(error);
    REQUIRE(assembly != nullptr);
    REQUIRE(error.empty());
    REQUIRE(assembly->config.opt_int("extruder") == 2);
    REQUIRE(assembly->volumes.front()->config.opt_int("extruder") == 2);
}

TEST_CASE("Flow ratio calibration preserves previously built assemblies", "[Calib]")
{
    Model model;
    std::vector<ModelObject*> first_members;
    std::vector<ModelObject*> second_members;
    for (size_t member_idx = 0; member_idx < 2; ++member_idx) {
        ModelObject* first_member = model.add_object(("flowrate_0.9" + std::to_string(member_idx)).c_str(), "", make_cube(10, 10, 10));
        first_member->add_instance();
        first_member->config.set("extruder", 1);
        first_members.push_back(first_member);

        ModelObject* second_member = model.add_object(("flowrate_1.1" + std::to_string(member_idx)).c_str(), "", make_cube(10, 10, 10));
        second_member->add_instance();
        second_member->config.set("extruder", 2);
        second_members.push_back(second_member);
    }

    std::string error;
    ModelObject* first_assembly = build_flow_ratio_calibration_assembly(model, first_members, 1, first_members.size(), error);
    ModelObject* second_assembly = build_flow_ratio_calibration_assembly(model, second_members, 2, second_members.size(), error);

    CAPTURE(error);
    REQUIRE(first_assembly != nullptr);
    REQUIRE(second_assembly != nullptr);
    REQUIRE(error.empty());
    REQUIRE(model.objects.size() == 2);
    REQUIRE(model.objects.front() == first_assembly);
    REQUIRE(model.objects.back() == second_assembly);
}

TEST_CASE("Flow ratio shared object settings set on any member must match", "[Calib]")
{
    Model model;
    std::vector<ModelObject *> members;
    for (const std::string &name : {"flowrate_0.98", "flowrate_1.02"}) {
        ModelObject *member = model.add_object(name.c_str(), "", make_cube(10, 10, 10));
        member->add_instance();
        member->config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stNormal));
        member->config.set("extruder", 2);
        members.push_back(member);
    }
    members.back()->config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTree));

    std::string error;
    ModelObject *assembly = build_flow_ratio_calibration_assembly(model, members, 2, members.size(), error);

    REQUIRE(assembly == nullptr);
    REQUIRE(error == "The Flow Ratio calibration members differ in the shared object setting support_type.");
}

TEST_CASE("Flow ratio modifiers parse from object, volume, and plain names", "[Calib]")
{
    Model model;
    ModelObject *object = model.add_object("flowrate_0.98", "", make_cube(10, 10, 10));

    REQUIRE_THAT(flow_ratio_calibration_print_flow_ratio("flowrate_0.98", 0.98, false).value(),
                 Catch::Matchers::WithinAbs(1.0098, 1e-8));
    REQUIRE_THAT(flow_ratio_calibration_print_flow_ratio(*object, 0.98, true).value(),
                 Catch::Matchers::WithinAbs(2.0, 1e-8));
    REQUIRE_THAT(flow_ratio_calibration_print_flow_ratio(*object->volumes.front(), 0.98, true).value(),
                 Catch::Matchers::WithinAbs(2.0, 1e-8));
    REQUIRE_FALSE(flow_ratio_calibration_print_flow_ratio("flowrate_invalid", 0.98, false).has_value());
}

namespace
{

Polygon calibration_layout_rectangle(double width, double height)
{
    return Polygon::new_scale({
        {0.0, 0.0},
        {width, 0.0},
        {width, height},
        {0.0, height},
    });
}

Points calibration_layout_bed(double width, double height)
{
    return Points{
        Point::new_scale(0.0, 0.0),
        Point::new_scale(width, 0.0),
        Point::new_scale(width, height),
        Point::new_scale(0.0, height),
    };
}

} // namespace

TEST_CASE("One or two flow ratio assemblies stack vertically in one column", "[Calib]")
{
    const std::vector<Polygon> hulls{
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
    };

    const auto single = arrange_flow_ratio_calibration_assemblies(
        {hulls.front()}, calibration_layout_bed(256.0, 256.0));
    REQUIRE(single.has_value());
    REQUIRE(single->size() == 1);
    REQUIRE_THAT(single->at(0).center.x(), Catch::Matchers::WithinAbs(128.0, 1e-6));
    REQUIRE_THAT(single->at(0).center.y(), Catch::Matchers::WithinAbs(128.0, 1e-6));
    REQUIRE_THAT(single->at(0).rotation, Catch::Matchers::WithinAbs(0.0, 1e-12));

    const auto pair = arrange_flow_ratio_calibration_assemblies(hulls, calibration_layout_bed(256.0, 256.0));
    REQUIRE(pair.has_value());
    REQUIRE(pair->size() == 2);
    REQUIRE_THAT(pair->at(0).center.x(), Catch::Matchers::WithinAbs(128.0, 1e-6));
    REQUIRE_THAT(pair->at(0).center.y(), Catch::Matchers::WithinAbs(86.0, 1e-6));
    REQUIRE_THAT(pair->at(1).center.x(), Catch::Matchers::WithinAbs(128.0, 1e-6));
    REQUIRE_THAT(pair->at(1).center.y(), Catch::Matchers::WithinAbs(170.0, 1e-6));
    REQUIRE_THAT(pair->at(0).rotation, Catch::Matchers::WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(pair->at(1).rotation, Catch::Matchers::WithinAbs(0.0, 1e-12));
}

TEST_CASE("Three flow ratio assemblies use a centered two-column layout with clearance", "[Calib]")
{
    const std::vector<Polygon> hulls{
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
    };
    const auto placements = arrange_flow_ratio_calibration_assemblies(hulls, calibration_layout_bed(256.0, 256.0));

    REQUIRE(placements.has_value());
    REQUIRE(placements->size() == 3);
    REQUIRE_THAT(placements->at(0).center.x(), Catch::Matchers::WithinAbs(76.0, 1e-6));
    REQUIRE_THAT(placements->at(0).center.y(), Catch::Matchers::WithinAbs(86.0, 1e-6));
    REQUIRE_THAT(placements->at(1).center.x(), Catch::Matchers::WithinAbs(76.0, 1e-6));
    REQUIRE_THAT(placements->at(1).center.y(), Catch::Matchers::WithinAbs(170.0, 1e-6));
    REQUIRE_THAT(placements->at(2).center.x(), Catch::Matchers::WithinAbs(180.0, 1e-6));
    REQUIRE_THAT(placements->at(2).center.y(), Catch::Matchers::WithinAbs(86.0, 1e-6));
    REQUIRE_THAT(placements->at(0).rotation, Catch::Matchers::WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(placements->at(1).rotation, Catch::Matchers::WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(placements->at(2).rotation, Catch::Matchers::WithinAbs(0.0, 1e-12));
}

TEST_CASE("Four and five flow ratio assemblies use two-column multi-row layouts", "[Calib]")
{
    const std::vector<Polygon> four_hulls{
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
    };
    const auto four = arrange_flow_ratio_calibration_assemblies(four_hulls, calibration_layout_bed(256.0, 256.0));

    REQUIRE(four.has_value());
    REQUIRE(four->size() == 4);
    const std::array<Vec2d, 4> four_centers{{
        Vec2d(76.0, 86.0),
        Vec2d(76.0, 170.0),
        Vec2d(180.0, 86.0),
        Vec2d(180.0, 170.0),
    }};
    for (size_t assembly_idx = 0; assembly_idx < four_centers.size(); ++assembly_idx) {
        REQUIRE_THAT(four->at(assembly_idx).center.x(),
                     Catch::Matchers::WithinAbs(four_centers[assembly_idx].x(), 1e-6));
        REQUIRE_THAT(four->at(assembly_idx).center.y(),
                     Catch::Matchers::WithinAbs(four_centers[assembly_idx].y(), 1e-6));
        REQUIRE_THAT(four->at(assembly_idx).rotation, Catch::Matchers::WithinAbs(0.0, 1e-12));
    }

    std::vector<Polygon> five_hulls(four_hulls);
    five_hulls.push_back(calibration_layout_rectangle(100.0, 80.0));
    const auto five = arrange_flow_ratio_calibration_assemblies(five_hulls, calibration_layout_bed(256.0, 256.0));

    REQUIRE(five.has_value());
    REQUIRE(five->size() == 5);
    const std::array<Vec2d, 5> five_centers{{
        Vec2d(76.0, 44.0),
        Vec2d(76.0, 128.0),
        Vec2d(76.0, 212.0),
        Vec2d(180.0, 44.0),
        Vec2d(180.0, 128.0),
    }};
    for (size_t assembly_idx = 0; assembly_idx < five_centers.size(); ++assembly_idx) {
        REQUIRE_THAT(five->at(assembly_idx).center.x(),
                     Catch::Matchers::WithinAbs(five_centers[assembly_idx].x(), 1e-6));
        REQUIRE_THAT(five->at(assembly_idx).center.y(),
                     Catch::Matchers::WithinAbs(five_centers[assembly_idx].y(), 1e-6));
    }
}

TEST_CASE("Flow ratio assemblies do not rotate to force a narrow bed fit", "[Calib]")
{
    const std::vector<Polygon> four_hulls{
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
    };

    const auto placements = arrange_flow_ratio_calibration_assemblies(
        four_hulls, calibration_layout_bed(190.0, 256.0));

    REQUIRE_FALSE(placements.has_value());
}

TEST_CASE("Four full-size flow ratio assemblies fit a 256 mm bed without rotation", "[Calib]")
{
    const std::vector<std::pair<double, double>> hull_sizes{{124.0, 94.0}, {124.0, 126.0}};
    for (const auto [width, height] : hull_sizes) {
        const std::vector<Polygon> hulls{
            calibration_layout_rectangle(width, height),
            calibration_layout_rectangle(width, height),
            calibration_layout_rectangle(width, height),
            calibration_layout_rectangle(width, height),
        };

        const auto placements = arrange_flow_ratio_calibration_assemblies(
            hulls, calibration_layout_bed(256.0, 256.0));

        REQUIRE(placements.has_value());
        REQUIRE(placements->size() == 4);
        for (const CalibrationAssemblyPlacement& placement : *placements)
            REQUIRE_THAT(placement.rotation, Catch::Matchers::WithinAbs(0.0, 1e-12));
    }
}

TEST_CASE("Four real flow ratio calibration assemblies fit a 256 mm bed", "[Calib]")
{
    const boost::filesystem::path resources_dir{boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path() / "resources"};
    for (const std::string filename : {"flowrate-test-pass1.3mf", "flowrate-test-pass2.3mf"}) {
        std::vector<Polygon> assembly_hulls;
        for (size_t extruder = 0; extruder < 4; ++extruder) {
            Model model;
            DynamicPrintConfig config;
            ConfigSubstitutionContext substitutions{ForwardCompatibilitySubstitutionRule::Disable};
            REQUIRE(load_3mf(
                (resources_dir / "calib" / "filament_flow" / filename).string().c_str(),
                config, substitutions, &model, false));
            REQUIRE(model.objects.size() > 1);
            model.center_instances_around_point(Vec2d(128.0, 128.0));
            std::vector<ModelObject*> members(model.objects.begin(), model.objects.end());
            std::string error;
            ModelObject* assembly = build_flow_ratio_calibration_assembly(
                model, members, static_cast<int>(extruder + 1), members.size(), error);
            CAPTURE(error);
            REQUIRE(assembly != nullptr);
            REQUIRE(assembly->instances.size() == 1);
            arrangement::ArrangePolygon arrange_polygon;
            assembly->instances.front()->get_arrange_polygon(&arrange_polygon);
            const Polygon hull = arrange_polygon.poly.contour;
            CAPTURE(unscale<double>(hull.bounding_box().size().x()), unscale<double>(hull.bounding_box().size().y()));
            assembly_hulls.push_back(hull);
        }

        const auto placements = arrange_flow_ratio_calibration_assemblies(
            assembly_hulls, calibration_layout_bed(256.0, 256.0));
        REQUIRE(placements.has_value());
    }
}

TEST_CASE("Mixed nozzle flow ratio placements respect each nozzle area", "[Calib]")
{
    const std::vector<Polygon> hulls{
        calibration_layout_rectangle(124.0, 94.0),
        calibration_layout_rectangle(124.0, 94.0),
        calibration_layout_rectangle(124.0, 94.0),
        calibration_layout_rectangle(124.0, 94.0),
    };
    const Polygon left_area{calibration_layout_bed(256.0, 256.0)};
    const Polygon right_area{calibration_layout_bed(224.0, 256.0)};
    const std::vector<CalibrationNozzle> nozzles{
        CalibrationNozzle::Right,
        CalibrationNozzle::Left,
        CalibrationNozzle::Right,
        CalibrationNozzle::Left,
    };

    const auto placements = arrange_flow_ratio_calibration_assemblies(
        hulls, nozzles, left_area, right_area);

    REQUIRE(placements.has_value());
    REQUIRE(placements->size() == hulls.size());
    std::vector<Polygon> placed_hulls;
    for (size_t assembly_idx = 0; assembly_idx < hulls.size(); ++assembly_idx) {
        REQUIRE_THAT(placements->at(assembly_idx).rotation,
                     Catch::Matchers::WithinAbs(0.0, 1e-12));

        Polygon placed_hull = hulls[assembly_idx];
        placed_hull.rotate(placements->at(assembly_idx).rotation);
        const BoundingBox rotated_box = placed_hull.bounding_box();
        const Point target = Point::new_scale(
            placements->at(assembly_idx).center.x(), placements->at(assembly_idx).center.y());
        placed_hull.translate(target - rotated_box.center());

        const Polygon& nozzle_area = nozzles[assembly_idx] == CalibrationNozzle::Left ? left_area : right_area;
        const double placed_area = std::abs(placed_hull.area());
        const double contained_area = std::abs(area(intersection(placed_hull, nozzle_area)));
        REQUIRE(std::abs(placed_area - contained_area) <= std::max(1.0, placed_area * 1e-6));

        for (const Polygon& previous_hull : placed_hulls) {
            const Polygons overlap = intersection(
                offset(placed_hull, scale_(2.0)), offset(previous_hull, scale_(2.0)));
            REQUIRE(area(overlap) <= 1.0);
        }
        placed_hulls.push_back(placed_hull);
    }
}

TEST_CASE("All-left flow ratio placements can use the full bed", "[Calib]")
{
    const std::vector<Polygon> hulls{
        calibration_layout_rectangle(124.0, 94.0),
        calibration_layout_rectangle(124.0, 94.0),
        calibration_layout_rectangle(124.0, 94.0),
        calibration_layout_rectangle(124.0, 94.0),
    };
    const Polygon left_area{calibration_layout_bed(256.0, 256.0)};
    const Polygon right_area{calibration_layout_bed(224.0, 256.0)};

    const auto placements = arrange_flow_ratio_calibration_assemblies(
        hulls,
        std::vector<CalibrationNozzle>(hulls.size(), CalibrationNozzle::Left),
        left_area,
        right_area);

    REQUIRE(placements.has_value());
    for (size_t assembly_idx = 0; assembly_idx < hulls.size(); ++assembly_idx) {
        Polygon placed_hull = hulls[assembly_idx];
        const Point target = Point::new_scale(
            placements->at(assembly_idx).center.x(), placements->at(assembly_idx).center.y());
        placed_hull.translate(target - placed_hull.bounding_box().center());
        const double placed_area = std::abs(placed_hull.area());
        const double contained_area = std::abs(area(intersection(placed_hull, left_area)));
        REQUIRE(std::abs(placed_area - contained_area) <= std::max(1.0, placed_area * 1e-6));
    }
}

TEST_CASE("All-right flow ratio placements respect the configured nozzle area", "[Calib]")
{
    const std::vector<Polygon> hulls{
        calibration_layout_rectangle(124.0, 94.0),
        calibration_layout_rectangle(124.0, 94.0),
        calibration_layout_rectangle(124.0, 94.0),
    };
    const Polygon left_area{calibration_layout_bed(256.0, 256.0)};
    const Polygon right_area{calibration_layout_bed(224.0, 256.0)};

    const auto placements = arrange_flow_ratio_calibration_assemblies(
        hulls,
        std::vector<CalibrationNozzle>(hulls.size(), CalibrationNozzle::Right),
        left_area,
        right_area);

    REQUIRE_FALSE(placements.has_value());
}

TEST_CASE("All-left flow ratio placements respect the configured nozzle area", "[Calib]")
{
    const std::vector<Polygon> hulls{
        calibration_layout_rectangle(124.0, 94.0),
        calibration_layout_rectangle(124.0, 94.0),
        calibration_layout_rectangle(124.0, 94.0),
    };
    const Polygon left_area{calibration_layout_bed(224.0, 256.0)};
    const Polygon right_area{calibration_layout_bed(256.0, 256.0)};

    const auto placements = arrange_flow_ratio_calibration_assemblies(
        hulls,
        std::vector<CalibrationNozzle>(hulls.size(), CalibrationNozzle::Left),
        left_area,
        right_area);

    REQUIRE_FALSE(placements.has_value());
}

TEST_CASE("Flow ratio placement transforms whole assembly hulls onto the bed", "[Calib]")
{
    const std::vector<Polygon> hulls{
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
    };
    const Points bed = calibration_layout_bed(256.0, 256.0);
    const auto placements = arrange_flow_ratio_calibration_assemblies(hulls, bed);
    REQUIRE(placements.has_value());

    for (size_t assembly_idx = 0; assembly_idx < hulls.size(); ++assembly_idx) {
        Polygon placed_hull = hulls[assembly_idx];
        placed_hull.rotate(placements->at(assembly_idx).rotation);
        const BoundingBox rotated_box = placed_hull.bounding_box();
        const Point target = Point::new_scale(
            placements->at(assembly_idx).center.x(), placements->at(assembly_idx).center.y());
        placed_hull.translate(target - rotated_box.center());

        const double placed_area = std::abs(placed_hull.area());
        const double contained_area = std::abs(area(intersection(placed_hull, Polygon(bed))));
        REQUIRE(std::abs(placed_area - contained_area) <= std::max(1.0, placed_area * 1e-6));
    }
}

TEST_CASE("Flow ratio assembly layout fits non-convex bed geometry", "[Calib]")
{
    const std::vector<Polygon> hulls{
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
        calibration_layout_rectangle(100.0, 80.0),
    };
    const Points non_convex_bed{
        Point::new_scale(0.0, 0.0),
        Point::new_scale(256.0, 0.0),
        Point::new_scale(256.0, 256.0),
        Point::new_scale(180.0, 256.0),
        Point::new_scale(180.0, 160.0),
        Point::new_scale(80.0, 160.0),
        Point::new_scale(80.0, 256.0),
        Point::new_scale(0.0, 256.0),
    };

    const auto placements = arrange_flow_ratio_calibration_assemblies(hulls, non_convex_bed);

    REQUIRE_FALSE(placements.has_value());
}

namespace {

struct EndState { double final_e; double max_e; };

EndState simulate_absolute_e(const std::string &gcode)
{
    double final_e = 0.;
    double max_e   = 0.;

    std::istringstream lines(gcode);
    std::string        line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string        op;
        if (!(words >> op))
            continue;
        if (op != "G1" && op != "G0" && op != "G92")
            continue;

        std::string word;
        while (words >> word) {
            if (word.size() >= 2 && word[0] == 'E') {
                final_e = std::stod(word.substr(1));
                max_e   = std::max(max_e, final_e);
                break;
            }
        }
    }

    return {final_e, max_e};
}

} // namespace

TEST_CASE("PA pattern resets the extruder after the final layer in absolute E mode", "[Calib][Regression]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"use_relative_e_distances", "0"},
        {"line_width", "0.45"},
        {"initial_layer_line_width", "0.45"},
    });

    Model model;
    model.add_object("cube", "", make_cube(20, 20, 20))->add_instance();

    Calib_Params params;
    params.mode  = CalibMode::Calib_PA_Pattern;
    params.start = 0.;
    params.end   = 0.08;
    params.step  = 0.002;

    CalibPressureAdvancePattern pattern(params, config, /* is_bbl_machine */ false, *model.objects.front(), Vec3d(0, 0, 0));
    const CustomGCode::Info     info = pattern.generate_custom_gcodes(config, /* is_bbl_machine */ false, *model.objects.front(),
                                                                      Vec3d(0, 0, 0));

    std::string gcode;
    for (const CustomGCode::Item &item : info.gcodes)
        gcode += item.extra;

    const EndState state = simulate_absolute_e(gcode);

    REQUIRE(state.max_e > 1.);
    REQUIRE_THAT(state.final_e, Catch::Matchers::WithinAbs(0., 1e-9));
}

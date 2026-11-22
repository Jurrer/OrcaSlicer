#include <boost/filesystem/operations.hpp>
#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelArrange.hpp"

#include <boost/filesystem.hpp>
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <vector>
#include "libslic3r/Point.hpp"
#include <cstddef>

#include "test_helpers.hpp"
#include "test_utils.hpp"
#include "libslic3r/Arrange.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/calib.hpp"

using namespace Slic3r;

TEST_CASE("Four short flow ratio assemblies pass sequential print clearance", "[FFFPrint][Calib]")
{
    Model model;
    const std::array<Vec2d, 4> centers{
        Vec2d(62.0, 49.0),
        Vec2d(62.0, 147.0),
        Vec2d(190.0, 49.0),
        Vec2d(190.0, 147.0),
    };
    for (const Vec2d& center : centers) {
        ModelObject* object = model.add_object("flow ratio assembly", "", make_cube(124.0, 94.0, 2.0));
        object->add_instance()->set_offset(Vec3d(center.x(), center.y(), 0.0));
    }

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("nozzle_height", new ConfigOptionFloat(4.76f));
    config.set_key_value("extruder_clearance_radius", new ConfigOptionFloat(40.0f));
    config.set_key_value("skirt_loops", new ConfigOptionInt(0));
    config.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByObject));

    Print print;
    for (ModelObject* object : model.objects) {
        object->ensure_on_bed();
        print.auto_assign_extruders(object);
    }
    print.apply(model, config);

    const StringObjectException clearance_exception = print.sequential_print_clearance_valid(print);
    CAPTURE(clearance_exception.string);
    if (!clearance_exception.string.empty())
        FAIL(clearance_exception.string);
    REQUIRE(clearance_exception.string.empty());
}

TEST_CASE("Four real flow ratio assemblies arrange and pass sequential clearance", "[FFFPrint][Calib]")
{
    const boost::filesystem::path resources_dir{
        boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path() / "resources"};
    Model model;
    std::vector<Polygon> assembly_hulls;
    std::vector<ModelInstance*> instances;
    for (size_t extruder = 1; extruder <= 4; ++extruder) {
        Model resource_model;
        DynamicPrintConfig resource_config;
        ConfigSubstitutionContext substitutions{ForwardCompatibilitySubstitutionRule::Disable};
        REQUIRE(load_3mf(
            (resources_dir / "calib" / "filament_flow" / "flowrate-test-pass1.3mf").string().c_str(),
            resource_config, substitutions, &resource_model, false));
        REQUIRE(resource_model.objects.size() > 1);
        resource_model.center_instances_around_point(Vec2d(128.0, 128.0));
        std::vector<ModelObject*> members(resource_model.objects.begin(), resource_model.objects.end());
        std::string error;
        ModelObject* resource_assembly = build_flow_ratio_calibration_assembly(
            resource_model, members, static_cast<int>(extruder), members.size(), error);
        CAPTURE(error);
        REQUIRE(resource_assembly != nullptr);

        ModelObject* assembly = model.add_object(*resource_assembly);
        assembly->name = resource_assembly->name;
        arrangement::ArrangePolygon arrange_polygon;
        assembly->instances.front()->get_arrange_polygon(&arrange_polygon);
        assembly_hulls.push_back(arrange_polygon.poly.contour);
        instances.push_back(assembly->instances.front());
    }

    const Points bed{
        Point::new_scale(0.0, 0.0),
        Point::new_scale(256.0, 0.0),
        Point::new_scale(256.0, 256.0),
        Point::new_scale(0.0, 256.0),
    };
    const auto placements = arrange_flow_ratio_calibration_assemblies(assembly_hulls, bed);
    REQUIRE(placements.has_value());
    for (size_t assembly_idx = 0; assembly_idx < instances.size(); ++assembly_idx) {
        ModelInstance* instance = instances[assembly_idx];
        const Vec3d current_offset = instance->get_offset();
        const Vec3d current_center = instance->get_object()->instance_bounding_box(0, false).center();
        const Vec2d placement_offset(
            current_offset.x() + placements->at(assembly_idx).center.x() - current_center.x(),
            current_offset.y() + placements->at(assembly_idx).center.y() - current_center.y());
        instance->apply_arrange_result(
            Vec2d(scale_(placement_offset.x()), scale_(placement_offset.y())),
            placements->at(assembly_idx).rotation);
    }


    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("printable_area", new ConfigOptionPoints({Vec2d(0.0, 0.0), Vec2d(256.0, 0.0), Vec2d(256.0, 256.0), Vec2d(0.0, 256.0)}));
    config.set_key_value("nozzle_height", new ConfigOptionFloat(4.0f));
    config.set_key_value("extruder_clearance_radius", new ConfigOptionFloat(40.0f));
    config.set_key_value("skirt_loops", new ConfigOptionInt(0));
    config.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByObject));
    Print print;
    for (ModelObject* object : model.objects) {
        object->ensure_on_bed();
        print.auto_assign_extruders(object);
    }
    print.apply(model, config);
    const StringObjectException clearance_exception = print.sequential_print_clearance_valid(print);
    REQUIRE(clearance_exception.string.empty());
}

SCENARIO("Model construction", "[Model]") {
    GIVEN("A Slic3r Model") {
		Slic3r::Model model;
        Slic3r::TriangleMesh sample_mesh = Slic3r::make_cube(20,20,20);
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        Slic3r::Print print;

        WHEN("Model object is added") {
            Slic3r::ModelObject *model_object = model.add_object();
            THEN("Model object list == 1") {
                REQUIRE(model.objects.size() == 1);
            }
            model_object->add_volume(sample_mesh);
            THEN("Model volume list == 1") {
                REQUIRE(model_object->volumes.size() == 1);
            }
            THEN("Model volume is a part") {
                REQUIRE(model_object->volumes.front()->is_model_part());
            }
            THEN("Mesh is equivalent to input mesh.") {
                REQUIRE(! sample_mesh.its.vertices.empty());
				const std::vector<Vec3f>& mesh_vertices = model_object->volumes.front()->mesh().its.vertices;
				Vec3f mesh_offset = model_object->volumes.front()->source.mesh_offset.cast<float>();
				for (size_t i = 0; i < sample_mesh.its.vertices.size(); ++ i) {
					const Vec3f &p1 = sample_mesh.its.vertices[i];
					const Vec3f  p2 = mesh_vertices[i] + mesh_offset;
					REQUIRE((p2 - p1).norm() < EPSILON);
				}
            }
            model_object->add_instance();
            arrange_objects(model, InfiniteBed{scaled(Vec2d(100, 100))}, ArrangeParams{scaled(min_object_distance(config))});
			model_object->ensure_on_bed();
			print.auto_assign_extruders(model_object);
			THEN("Print works?") {
				print.set_status_silent();
				print.apply(model, config);
				print.process();
				ScopedTemporaryFile temp(".gcode");
                print.export_gcode(temp.string(), nullptr, nullptr);
                REQUIRE(boost::filesystem::exists(temp.path()));
				REQUIRE(boost::filesystem::is_regular_file(temp.path()));
				REQUIRE(boost::filesystem::file_size(temp.path()) > 0);
			}
        }
    }
}

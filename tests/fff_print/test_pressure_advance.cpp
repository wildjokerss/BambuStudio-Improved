#include <catch2/catch.hpp>

// The GUI normally supplies NanoSVG's implementation. Export tests link only
// the slicing library, so provide the same parser without the GUI dependency.
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"

#include "libslic3r/GCodeWriter.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <boost/filesystem.hpp>
#include <algorithm>
#include <fstream>
#include <limits>
#include <locale>
#include <optional>
#include <nlohmann/json.hpp>

using namespace Slic3r;

namespace {
struct TemporaryDirectory {
    boost::filesystem::path path = boost::filesystem::temp_directory_path() /
                                   boost::filesystem::unique_path("filament-pa-%%%%-%%%%");
    TemporaryDirectory() { boost::filesystem::create_directories(path); }
    ~TemporaryDirectory() { boost::system::error_code ec; boost::filesystem::remove_all(path, ec); }
};

struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};

struct ScopedLocale {
    std::locale previous = std::locale();
    ScopedLocale() { std::locale::global(std::locale(previous, new CommaDecimal)); }
    ~ScopedLocale() { std::locale::global(previous); }
};

std::string slice_with_pressure_advance(bool enabled, bool multi_filament = false, bool prime_tower = false,
                                      std::optional<ExtruderType> pa_extruder = std::nullopt, bool dual_nozzle = false,
                                      bool main_pa_enabled = true)
{
    // Create dynamic defaults through the definition so vector-enum options
    // include the lookup tables required when exporting the configuration.
    DynamicPrintConfig config;
    for (const auto &key : FullPrintConfig::defaults().keys())
        config.option(key, true);
    for (const auto &key : filament_options_with_variant)
        config.option(key, true);
    if (multi_filament) {
        for (const auto &key : Preset::filament_options()) {
            auto *option = config.option(key);
            if (option && option->is_vector())
                static_cast<ConfigOptionVectorBase *>(option)->resize(2, option);
        }
    }
    config.set_deserialize_strict({
        {"enable_pressure_advance", enabled ? "1" : "0"},
        {"pressure_advance", "0.035"},
        {"machine_start_gcode", "M900 K0.9\n;PA_MACHINE_START_END\n"},
        {"enable_prime_tower", prime_tower},
        {"single_extruder_multi_material", true},
        {"enable_support", false},
        {"brim_type", "no_brim"},
        {"skirt_loops", 0}
    });
    config.option<ConfigOptionStrings>("filament_start_gcode")->values = {";PA_FILAMENT_START_END\n"};
    if (pa_extruder) {
        REQUIRE_FALSE(multi_filament);
        config.option<ConfigOptionEnumsGeneric>("extruder_type")->values = {*pa_extruder};
        config.option<ConfigOptionStrings>("filament_extruder_variant")->values = {"Direct Drive Standard", "Bowden Standard"};
        config.option<ConfigOptionInts>("filament_self_index")->values = {1, 1};
        config.option<ConfigOptionBools>("enable_pressure_advance")->values = {enabled, enabled};
        config.option<ConfigOptionFloats>("pressure_advance")->values = {0.035, 0.4};
    }
    if (multi_filament) {
        config.option<ConfigOptionBools>("enable_pressure_advance")->values = {enabled, enabled};
        config.option<ConfigOptionFloats>("pressure_advance")->values = {0.035, 0.055};
        config.option<ConfigOptionInts>("filament_map")->values = {1, 1};
        config.option<ConfigOptionInts>("filament_self_index")->values = {1, 2};
        config.option<ConfigOptionStrings>("filament_colour")->values = {"#FF0000", "#00FF00"};
        config.option<ConfigOptionFloats>("flush_volumes_matrix")->values = {0., 100., 100., 0.};
        config.option<ConfigOptionStrings>("filament_start_gcode")->values = {
            "M900 K0.9\n;PA_FILAMENT_0_START_END\n",
            "M900 K0.9\n;PA_FILAMENT_1_START_END\n"};
        config.set_deserialize_strict("change_filament_gcode", "T[next_filament_id]\nM900 K0.9\n");
    }
    if (dual_nozzle) {
        REQUIRE(multi_filament);
        for (const auto &keys : {printer_options_with_variant_1, printer_options_with_variant_2, print_options_with_variant}) {
            for (const auto &key : keys) {
                auto *option = config.option(key, true);
                if (option && option->is_vector())
                    static_cast<ConfigOptionVectorBase *>(option)->resize(printer_options_with_variant_2.count(key) ? 4 : 2, option);
            }
        }
        for (const auto &key : filament_options_with_variant) {
            auto *option = config.option(key, true);
            static_cast<ConfigOptionVectorBase *>(option)->resize(4, option);
        }
        config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values = {0.4, 0.4};
        config.option<ConfigOptionEnumsGeneric>("extruder_type")->values = {etDirectDrive, etBowden};
        config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values = {nvtStandard, nvtStandard};
        config.option<ConfigOptionStrings>("extruder_variant_list", true)->values = {"Direct Drive Standard", "Bowden Standard"};
        config.option<ConfigOptionStrings>("printer_extruder_variant")->values = {"Direct Drive Standard", "Bowden Standard"};
        config.option<ConfigOptionStrings>("print_extruder_variant")->values = {"Direct Drive Standard", "Bowden Standard"};
        config.option<ConfigOptionInts>("printer_extruder_id")->values = {1, 2};
        config.option<ConfigOptionInts>("print_extruder_id")->values = {1, 2};
        config.option<ConfigOptionInts>("filament_map")->values = {1, 2};
        config.option<ConfigOptionInts>("filament_volume_map")->values = {nvtStandard, nvtStandard};
        config.set_deserialize_strict("filament_map_mode", "Manual");
        config.option<ConfigOptionStrings>("filament_extruder_variant")->values = {
            "Direct Drive Standard", "Bowden Standard", "Direct Drive Standard", "Bowden Standard"};
        config.option<ConfigOptionInts>("filament_self_index")->values = {1, 1, 2, 2};
        config.option<ConfigOptionBools>("enable_pressure_advance")->values = {enabled, enabled, enabled, enabled};
        config.option<ConfigOptionBools>("enable_pressure_advance")->values[0] = enabled && main_pa_enabled;
        config.option<ConfigOptionFloats>("pressure_advance")->values = {0.035, 0.4, 0.035, 0.4};
        config.option<ConfigOptionFloats>("flush_multiplier")->values = {1., 1.};
        config.option<ConfigOptionFloats>("flush_volumes_matrix")->values = {0., 100., 100., 0., 0., 100., 100., 0.};
    }

    Model model;
    ModelObject *object = model.add_object();
    object->name = "pa-test.stl";
    object->add_volume(make_cube(10., 10., 0.6));
    object->add_instance();
    object->instances.front()->set_offset(Vec3d(100., 100., 0.));
    object->ensure_on_bed();
    if (multi_filament) {
        object->config.set_key_value("extruder", new ConfigOptionInt(1));
        ModelObject *second = model.add_object();
        second->name = "second-filament.stl";
        second->add_volume(make_cube(10., 10., 0.6));
        second->add_instance();
        second->instances.front()->set_offset(Vec3d(125., 100., 0.));
        second->ensure_on_bed();
        second->config.set_key_value("extruder", new ConfigOptionInt(2));
    }

    Print print;
    print.set_BBL_Printer(true);
    print.set_status_silent();
    print.set_no_check_flag(true);
    for (auto *model_object : model.objects)
        print.auto_assign_extruders(model_object);
    print.apply(model, config);
    print.process();

    if (!dual_nozzle) {
        auto groups = MultiNozzleUtils::LayeredNozzleGroupResult::create(
            multi_filament ? std::vector<int>{0, 0} : std::vector<int>{0},
            std::vector<MultiNozzleUtils::NozzleInfo>{{"0.4", nvtStandard, 0, 0}},
            multi_filament ? std::vector<unsigned int>{0, 1} : std::vector<unsigned int>{0});
        REQUIRE(groups.has_value());
        print.set_nozzle_group_result(std::make_shared<MultiNozzleUtils::LayeredNozzleGroupResult>(*groups));
    } else {
        const auto groups = print.get_layered_nozzle_group_result();
        REQUIRE(groups);
        REQUIRE(groups->get_extruder_id(0, 0) == 0);
        REQUIRE(groups->get_extruder_id(1, 0) == 1);
    }

    TemporaryDirectory output;
    auto path = (output.path / "pressure-advance.gcode").string();
    GCodeProcessorResult result;
    print.export_gcode(path, &result, nullptr);
    std::ifstream stream(path);
    REQUIRE(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
} // namespace

TEST_CASE("Filament pressure advance survives preset save and reload", "[PressureAdvance]")
{
    DynamicPrintConfig parent = DynamicPrintConfig::full_print_config();
    REQUIRE_FALSE(parent.option<ConfigOptionBools>("enable_pressure_advance")->get_at(0));
    REQUIRE(print_config_def.get("pressure_advance")->min == 0.);
    const auto &keys = Preset::filament_options();
    REQUIRE(std::find(keys.begin(), keys.end(), "pressure_advance") != keys.end());
    REQUIRE(std::find(keys.begin(), keys.end(), "enable_pressure_advance") != keys.end());

    Preset preset(Preset::TYPE_FILAMENT, "X2D PA filament");
    preset.config = parent;
    preset.config.set_deserialize_strict("enable_pressure_advance", "1");
    preset.config.set_deserialize_strict("pressure_advance", "0.035");
    TemporaryDirectory output;
    preset.file = (output.path / "filament.json").string();
    REQUIRE(preset.save(&parent));
    std::ifstream saved(preset.file);
    const auto json = nlohmann::json::parse(saved);
    REQUIRE(json.at("enable_pressure_advance") == nlohmann::json::array({"1"}));
    REQUIRE(json.at("pressure_advance") == nlohmann::json::array({"0.035"}));

    DynamicPrintConfig loaded;
    std::map<std::string, std::string> metadata;
    std::string reason;
    loaded.load_from_json(preset.file, ForwardCompatibilitySubstitutionRule::Disable, metadata, reason);
    REQUIRE(reason.empty());
    REQUIRE(loaded.option<ConfigOptionBools>("enable_pressure_advance")->get_at(0));
    REQUIRE(loaded.option<ConfigOptionFloats>("pressure_advance")->get_at(0) == Approx(0.035));
}

TEST_CASE("X2D pressure advance uses the filament's mapped extruder", "[PressureAdvance]")
{
    GCodeWriter writer;
    writer.set_is_bbl_printer(true);
    // Both filament presets contain independent direct-drive and Bowden values.
    writer.config.filament_extruder_variant.values = {
        "Direct Drive Standard", "Bowden Standard", "Direct Drive Standard", "Bowden Standard"};
    writer.config.filament_self_index.values = {1, 1, 2, 2};
    writer.config.enable_pressure_advance.values = {true, true, true, false};
    writer.config.pressure_advance.values = {0.025, 0.4, 0.035, 0.5};
    writer.config.extruder_type.values = {etDirectDrive, etBowden};
    writer.config.filament_map.values = {2, 1};

    REQUIRE_THAT(writer.set_filament_pressure_advance(0), Catch::Contains("M901 P0.75 K0.4"));
    REQUIRE_THAT(writer.set_filament_pressure_advance(1), Catch::Contains("M900 K0.035 L1000 M10"));

    // The same filament changes value and command when its head changes.
    writer.config.filament_map.values[0] = 1;
    REQUIRE_THAT(writer.set_filament_pressure_advance(0), Catch::Contains("M900 K0.025 L1000 M10"));
    writer.config.filament_map.values[0] = 2;
    REQUIRE_THAT(writer.set_filament_pressure_advance(0), Catch::Contains("M901 P0.75 K0.4"));

    // Enablement is also per variant, so the other head can retain calibration.
    writer.config.filament_map.values[1] = 2;
    REQUIRE(writer.set_filament_pressure_advance(1).empty());
    writer.config.enable_pressure_advance.values[3] = true;
    REQUIRE_THAT(writer.set_filament_pressure_advance(1), Catch::Contains("M901 P0.75 K0.5"));
    writer.config.pressure_advance.values[3] = 0.;
    REQUIRE_THAT(writer.set_filament_pressure_advance(1), Catch::Contains("M901 P0.75 K0;"));
}

TEST_CASE("A filament preset saves independent PA values for both extruder types", "[PressureAdvance]")
{
    DynamicPrintConfig parent = DynamicPrintConfig::full_print_config();
    parent.option<ConfigOptionStrings>("filament_extruder_variant")->values = {"Direct Drive Standard", "Bowden Standard"};
    Preset::normalize(parent);
    REQUIRE(filament_options_with_variant.count("enable_pressure_advance") == 1);
    REQUIRE(filament_options_with_variant.count("pressure_advance") == 1);
    Preset preset(Preset::TYPE_FILAMENT, "Dual-drive PA filament");
    preset.config = parent;
    preset.config.option<ConfigOptionBools>("enable_pressure_advance")->values = {true, false};
    preset.config.option<ConfigOptionFloats>("pressure_advance")->values = {0.035, 0.4};
    TemporaryDirectory output;
    preset.file = (output.path / "dual-drive-filament.json").string();
    REQUIRE(preset.save(&parent));

    DynamicPrintConfig saved;
    std::map<std::string, std::string> metadata;
    std::string reason;
    saved.load_from_json(preset.file, ForwardCompatibilitySubstitutionRule::Disable, metadata, reason);
    REQUIRE(reason.empty());

    // Exercise the same variant-aware parent/child merge used when loading presets.
    auto loaded = parent;
    std::set<std::string> empty_keys;
    loaded.update_diff_values_to_child_config(saved, "", "filament_extruder_variant", filament_options_with_variant, empty_keys);
    Preset::normalize(loaded);
    REQUIRE(loaded.option<ConfigOptionFloats>("pressure_advance")->values == std::vector<double>{0.035, 0.4});
    REQUIRE(loaded.option<ConfigOptionBools>("enable_pressure_advance")->values == std::vector<unsigned char>{true, false});
}

TEST_CASE("Older shared PA values expand to variants without crossing filaments", "[PressureAdvance]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionStrings>("filament_extruder_variant")->values = {"Direct Drive Standard", "Bowden Standard"};
    config.option<ConfigOptionBools>("enable_pressure_advance")->values = {true};
    config.option<ConfigOptionFloats>("pressure_advance")->values = {0.035};
    SECTION("One old filament preset") {
        Preset::normalize(config);
        REQUIRE(config.option<ConfigOptionBools>("enable_pressure_advance")->values == std::vector<unsigned char>{true, true});
        REQUIRE(config.option<ConfigOptionFloats>("pressure_advance")->values == std::vector<double>{0.035, 0.035});
    }
    SECTION("An older project with two filaments") {
        config.option<ConfigOptionFloats>("filament_diameter")->values = {1.75, 1.75};
        config.option<ConfigOptionStrings>("filament_extruder_variant")->values = {
            "Direct Drive Standard", "Bowden Standard", "Direct Drive Standard", "Bowden Standard"};
        config.option<ConfigOptionInts>("filament_self_index")->values = {1, 1, 2, 2};
        config.option<ConfigOptionBools>("enable_pressure_advance")->values = {true, false};
        config.option<ConfigOptionFloats>("pressure_advance")->values = {0.035, 0.07};
        Preset::normalize(config);
        REQUIRE(config.option<ConfigOptionBools>("enable_pressure_advance")->values == std::vector<unsigned char>{true, true, false, false});
        REQUIRE(config.option<ConfigOptionFloats>("pressure_advance")->values == std::vector<double>{0.035, 0.035, 0.07, 0.07});
    }
}

TEST_CASE("Pressure advance preserves other firmware formats and valid G-code numbers", "[PressureAdvance]")
{
    GCodeWriter writer;
    writer.config.enable_pressure_advance.values = {true};
    writer.config.pressure_advance.values = {0.035};
    SECTION("Klipper") {
        writer.config.gcode_flavor.value = gcfKlipper;
        REQUIRE_THAT(writer.set_filament_pressure_advance(0), Catch::Contains("SET_PRESSURE_ADVANCE ADVANCE=0.035"));
    }
    SECTION("RepRapFirmware") {
        writer.config.gcode_flavor.value = gcfRepRapFirmware;
        REQUIRE_THAT(writer.set_filament_pressure_advance(0), Catch::Contains("M572 D0 S0.035"));
    }
    SECTION("Marlin") {
        writer.config.gcode_flavor.value = gcfMarlinFirmware;
        REQUIRE_THAT(writer.set_filament_pressure_advance(0), Catch::Contains("M900 K0.035;"));
        REQUIRE(writer.set_filament_pressure_advance(0).find("L1000") == std::string::npos);
    }
    SECTION("Decimal-comma locale") {
        ScopedLocale locale;
        writer.set_is_bbl_printer(true);
        REQUIRE_THAT(writer.set_filament_pressure_advance(0), Catch::Contains("K0.035 L1000"));
    }
    SECTION("Invalid values") {
        for (double pa : {-0.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
            REQUIRE(writer.set_pressure_advance(pa).empty());
    }
}

TEST_CASE("Bambu G-code applies filament PA after startup or leaves calibration alone", "[PressureAdvance][GCodeExport]")
{
    const bool enabled = GENERATE(false, true);
    CAPTURE(enabled);
    const std::string gcode = slice_with_pressure_advance(enabled);
    const auto machine_start = gcode.find(";PA_MACHINE_START_END");
    const auto filament_start = gcode.find(";PA_FILAMENT_START_END");
    const auto override_pos = gcode.find("M900 K0.035 L1000 M10");
    REQUIRE(machine_start != std::string::npos);
    REQUIRE(filament_start != std::string::npos);
    if (enabled) {
        REQUIRE(override_pos != std::string::npos);
        REQUIRE(override_pos > machine_start);
        REQUIRE(override_pos > filament_start);
        const auto first_layer = gcode.find("; CHANGE_LAYER");
        REQUIRE(first_layer != std::string::npos);
        REQUIRE(override_pos < first_layer);
    } else {
        REQUIRE(override_pos == std::string::npos);
        REQUIRE(gcode.find("Override pressure advance value") == std::string::npos);
    }
}

TEST_CASE("Filament changes restore each profile's PA after custom G-code", "[PressureAdvance][GCodeExport]")
{
    const bool prime_tower = GENERATE(false, true);
    CAPTURE(prime_tower);
    const std::string gcode = slice_with_pressure_advance(true, true, prime_tower);
    std::istringstream lines(gcode);
    std::string line;
    int pending_filament = -1;
    int applied[2] = {0, 0};
    while (std::getline(lines, line)) {
        if (line == ";PA_FILAMENT_0_START_END" || line == ";PA_FILAMENT_1_START_END") {
            REQUIRE(pending_filament == -1);
            pending_filament = line == ";PA_FILAMENT_0_START_END" ? 0 : 1;
        } else if (line.find("Override pressure advance value") != std::string::npos) {
            REQUIRE(pending_filament != -1);
            REQUIRE_THAT(line, Catch::Contains(pending_filament == 0 ? "M900 K0.035 L1000 M10" : "M900 K0.055 L1000 M10"));
            ++applied[pending_filament];
            pending_filament = -1;
        }
    }
    REQUIRE(pending_filament == -1);
    REQUIRE(applied[0] > 0);
    REQUIRE(applied[1] > 0);
    REQUIRE(applied[0] + applied[1] > 2);
}

TEST_CASE("Export selects the same filament's PA for its active extruder type", "[PressureAdvance][GCodeExport]")
{
    const auto extruder = GENERATE(etDirectDrive, etBowden);
    CAPTURE(extruder);
    const auto gcode = slice_with_pressure_advance(true, false, false, extruder);
    const auto start = gcode.find(";PA_FILAMENT_START_END");
    const auto command = gcode.find(extruder == etBowden ? "M901 P0.75 K0.4" : "M900 K0.035 L1000 M10");
    REQUIRE(start != std::string::npos);
    REQUIRE(command != std::string::npos);
    REQUIRE(command > start);
    REQUIRE(command < gcode.find("; CHANGE_LAYER"));
    REQUIRE(gcode.find(extruder == etBowden ? "M900 K0.035 L1000 M10" : "M901 P0.75 K0.4") == std::string::npos);
}

TEST_CASE("Two cubes on main and auxiliary nozzles respect both PA settings", "[PressureAdvance][GCodeExport]")
{
    const bool prime_tower = GENERATE(false, true);
    const bool main_pa_enabled = GENERATE(false, true);
    CAPTURE(prime_tower, main_pa_enabled);
    const auto gcode = slice_with_pressure_advance(true, true, prime_tower, std::nullopt, true, main_pa_enabled);
    REQUIRE((gcode.find("M900 K0.035 L1000 M10") != std::string::npos) == main_pa_enabled);
    REQUIRE_THAT(gcode, Catch::Contains("M901 P0.75 K0.4"));
}

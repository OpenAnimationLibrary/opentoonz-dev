#include "image_loader.h"
#include "lut_fit.h"
#include "lut_writer.h"
#include "palette_fit.h"

#include <fstream>
#include <filesystem>
#include <vector>
#include <sstream>
#include <locale>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void printUsage(std::ostream& output) {
  output
      << "otlut - OpenToonz-oriented LUT generator\n\n"
      << "Usage:\n"
      << "  otlut --identity --output <file.cube|file.3dl> [options]\n"
      << "  otlut --source <before.png> --target <after.png> "
         "--output <look.cube|look.3dl> [options]\n\n"
      << "  otlut --color-pairs <pairs.txt> --output <look.cube|look.3dl> "
         "[options]\n\n"
      << "Options:\n"
      << "  --source <file>          Original/source image\n"
      << "  --target <file>          Graded/target image\n"
      << "  --color-pairs <file>     Rows: source RGB, target RGB, tolerance "
         "(0..1), label\n"
      << "  --size <n>               LUT grid size (default: 33)\n"
      << "  --output-bit-depth <n>   .3dl integer output depth (default: 12)\n"
      << "  --title <text>           LUT title metadata\n"
      << "  --help                   Show this help\n\n"
      << "Image formats in this stage: PNG, JPEG, BMP, TGA, PSD, GIF, HDR, "
         "PIC, PNM.\n"
      << ".3dl sizes compatible with OpenToonz: 2, 3, 5, 9, 17, 33, 65, 129\n";
}

int parseInt(const std::string& text, const std::string& option) {
  std::size_t used = 0;
  int value        = 0;
  try {
    value = std::stoi(text, &used);
  } catch (...) {
    throw std::invalid_argument(option + " requires an integer.");
  }
  if (used != text.size()) {
    throw std::invalid_argument(option + " requires an integer.");
  }
  return value;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
#ifdef _WIN32
    // QProcess supplies Unicode arguments. Use the wide command line rather
    // than the CRT's current-code-page argv, then pass UTF-8 to stb_image.
    int wideCount = 0;
    LPWSTR* wide  = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (!wide) throw std::runtime_error("Could not read command line.");
    std::vector<std::string> utf8;
    for (int i = 0; i < wideCount; ++i) {
      const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0,
                                            nullptr, nullptr);
      std::string arg(bytes, '\0');
      WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, &arg[0], bytes, nullptr,
                          nullptr);
      arg.pop_back();
      utf8.push_back(arg);
    }
    LocalFree(wide);
    std::vector<char*> narrow;
    for (auto& arg : utf8) narrow.push_back(&arg[0]);
    argc = wideCount;
    argv = narrow.data();
#endif
    bool identity = false;
    std::string sourcePath;
    std::string targetPath;
    std::string outputPath;
    std::string pairsPath;
    int size = 33;
    otlut::LutWriteOptions writeOptions;

    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--help" || arg == "-h") {
        printUsage(std::cout);
        return 0;
      }
      if (arg == "--identity") {
        identity = true;
        continue;
      }

      auto requireValue = [&](const std::string& name) -> std::string {
        if (i + 1 >= argc) {
          throw std::invalid_argument(name + " requires a value.");
        }
        return argv[++i];
      };

      if (arg == "--source") {
        sourcePath = requireValue(arg);
      } else if (arg == "--color-pairs") {
        pairsPath = requireValue(arg);
      } else if (arg == "--target") {
        targetPath = requireValue(arg);
      } else if (arg == "--output" || arg == "-o") {
        outputPath = requireValue(arg);
      } else if (arg == "--size") {
        size = parseInt(requireValue(arg), arg);
      } else if (arg == "--output-bit-depth") {
        writeOptions.outputBitDepth = parseInt(requireValue(arg), arg);
      } else if (arg == "--title") {
        writeOptions.title = requireValue(arg);
      } else {
        throw std::invalid_argument("Unknown option: " + arg);
      }
    }

    if (outputPath.empty()) {
      throw std::invalid_argument("--output is required.");
    }
    if (identity &&
        (!sourcePath.empty() || !targetPath.empty() || !pairsPath.empty())) {
      throw std::invalid_argument(
          "--identity cannot be combined with --source/--target.");
    }
    if (!pairsPath.empty() && (!sourcePath.empty() || !targetPath.empty())) {
      throw std::invalid_argument(
          "--color-pairs cannot be combined with images.");
    }
    if (!identity && pairsPath.empty() &&
        (sourcePath.empty() || targetPath.empty())) {
      throw std::invalid_argument(
          "Provide both --source and --target, or use --identity.");
    }

    const otlut::LutFormat format = otlut::formatFromPath(outputPath);
    if (format == otlut::LutFormat::Cube && !otlut::isSupportedCubeSize(size)) {
      throw std::invalid_argument(".cube size must be from 2 to 129.");
    }
    if (format == otlut::LutFormat::ThreeDl &&
        !otlut::isSupportedThreeDlSize(size)) {
      throw std::invalid_argument(
          ".3dl size must be one of 2, 3, 5, 9, 17, 33, 65, or 129.");
    }

    otlut::Lut3D lut;
    otlut::FitReport report;

    if (identity) {
      lut = otlut::makeIdentityLut(size);
      if (writeOptions.title == "otlut LUT") {
        writeOptions.title = "otlut identity LUT";
      }
    } else if (!pairsPath.empty()) {
      std::ifstream pairsFile(std::filesystem::u8path(pairsPath));
      if (!pairsFile)
        throw std::runtime_error("Could not read color-pair file.");
      std::vector<otlut::ColorPair> pairs;
      std::string line;
      int lineNumber = 0;
      while (std::getline(pairsFile, line)) {
        ++lineNumber;
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        row >> std::ws;
        if (row.eof() || row.peek() == '#') continue;
        otlut::ColorPair pair;
        if (!(row >> pair.source[0] >> pair.source[1] >> pair.source[2] >>
              pair.target[0] >> pair.target[1] >> pair.target[2] >>
              pair.tolerance))
          throw std::invalid_argument("Invalid color-pair row " +
                                      std::to_string(lineNumber));
        std::getline(row >> std::ws, pair.label);
        pairs.push_back(pair);
      }
      if (pairs.empty())
        throw std::invalid_argument("Color-pair file is empty.");
      otlut::PaletteFitReport paletteReport;
      lut = otlut::fitLutFromColorPairs(pairs, size, &paletteReport);
      std::cout << "Changed colors: " << paletteReport.changedColors << "\n"
                << "Preserved colors: " << paletteReport.preservedColors << "\n"
                << "Maximum RGB error: " << paletteReport.maximumError << "\n";
    } else {
      const otlut::Image source = otlut::loadImage(sourcePath);
      const otlut::Image target = otlut::loadImage(targetPath);

      lut = otlut::fitLutFromImagePair(source, target, size, &report);

      std::cout << "Source: " << sourcePath << "\n"
                << "Target: " << targetPath << "\n"
                << "Image size: " << source.width << " x " << source.height
                << "\n"
                << "Pixels examined: " << report.pixelsExamined << "\n"
                << "Directly sampled LUT cells: " << report.directlySampledCells
                << " / " << report.totalCells << " (" << std::fixed
                << std::setprecision(1) << report.coveragePercent() << "%)\n"
                << "Interpolated/fill LUT cells: " << report.inferredCells
                << " / " << report.totalCells << " (" << std::fixed
                << std::setprecision(1) << report.inferredPercent() << "%)\n";
    }

    std::ofstream output(std::filesystem::u8path(outputPath),
                         std::ios::out | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("Could not open output file: " + outputPath);
    }

    if (format == otlut::LutFormat::Cube) {
      otlut::writeCube(output, lut, writeOptions);
    } else {
      otlut::writeThreeDl(output, lut, writeOptions);
    }

    if (!output) {
      throw std::runtime_error("Failed while writing output file.");
    }

    std::cout << "Wrote LUT: " << outputPath << "\n"
              << "Grid size: " << size << "\n";
    if (format == otlut::LutFormat::ThreeDl) {
      std::cout << "3DL output bit depth: " << writeOptions.outputBitDepth
                << "\n";
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "otlut: " << e.what() << "\n";
    std::cerr << "Use --help for usage.\n";
    return 2;
  }
}

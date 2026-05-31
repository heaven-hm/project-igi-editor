#include "pch.h"
#include "cli_handler.h"
#include "cli_tests.h"
#include "common.h"
#include "logger.h"
#include "parsers/graph_parser.h"
#include "parsers/mef_exporter.h"
#include "parsers/mef_native.h"
#include "parsers/mef_parser.h"
#include "parsers/dat_parser.h"
#include "parsers/mtp_parser.h"
#include "parsers/qsc_lexer.h"
#include "parsers/qsc_parser.h"
#include "parsers/qvm_compiler.h"
#include "parsers/qvm_decompiler.h"
#include "parsers/qvm_parser.h"
#include "parsers/res_parser.h"
#include "parsers/terrain_files.h"
#include "parsers/tex_parser.h"
#include "utils.h"
#include <filesystem>


#include <iostream>

bool CLIHandler::IsCLICommand(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "--mef" || arg == "--qsc" || arg == "--qvm" ||
        arg == "--res" || arg == "--mtp" || arg == "--dat" || arg == "--terrain" ||
        arg == "--tex" || arg == "--graph" || arg == "--run-tests" ||
        arg == "--extract-level" || arg == "--verify-level") {
      return true;
    }
  }
  return false;
}

void CLIHandler::PrintHelp() {
  std::cout
      << "IGI Editor v1.5.0 - CLI & Headless Tool\n\n"
      << "GUI Editor Mode Options:\n"
      << "  -level <num>                           Load specific level (1-14)\n"
      << "  -w <width> -h <height>                 Set window dimensions\n"
      << "  -draw_parts <bitmask>                  Selective loading "
         "(1=terrain, 2=sky, 4=objects, 8=flat_sky, 16=buildings, 32=props, "
         "64=AI)\n"
      << "  -stick_to_ground                       Enable ground sticking on "
         "load\n\n"
      << "Editor Keyboard Controls:\n"
      << "  W/S/A/D       : Movement (Forward/Backward/Left/Right)\n"
      << "  Q/Z           : Vertical Movement (Up/Down)\n"
      << "  F4            : Toggle Edit Cursor (Global Edit Mode)\n"
      << "  F3            : Toggle Collision / Clipping\n"
      << "  F2            : Toggle Terrain Painting Mode\n"
      << "  PageUp/Dn     : Adjust Movement Speed\n"
      << "  Alt+Enter     : Toggle fullscreen mode\n"
      << "  Left/Right    : Change roll\n\n"
      << "CLI Parsing & Testing Modes (Headless):\n"
      << "  --help                                 Show this message\n"
      << "  --run-tests                            Run all native C++ parser "
         "unit tests\n"
      << "  --mef <file.mef>                       Parse and print MEF model "
         "details\n"
      << "  --mef <file.mef> --export-obj <out.obj> Export MEF model to OBJ "
         "format\n"
      << "  --mef <file.mef> --export-mef <out.mef> Export MEF model to ASCII "
         "MEF format\n"
      << "  --mef <file.mef> --export-bundle <outdir> --dat <file.dat> --texdir <dir>\n"
         "                                         Export OBJ+MTL+TGA bundle folder\n"
      << "  --qsc <file.qsc> --compile <out.qvm>   Compile QSC to QVM (native "
         "in-process)\n"
      << "  --qsc <file.qsc> --lex                 Lex QSC and print tokens\n"
      << "  --qsc <file.qsc> --parse               Parse QSC and dump AST\n"
      << "  --qvm <file.qvm> --decompile <out.qsc> Decompile QVM back to QSC "
         "(native)\n"
      << "  --qvm <file.qvm>                       Parse QVM bytecode details\n"
      << "  --res <file.res>                       List RES archive contents\n"
      << "  --res <file.res> --extract <name> <out> Extract specific resource\n"
      << "  --res <file.res> --extract-all <dir>   Extract all resources to "
         "directory\n"
      << "  --mtp <file.mtp>                       Parse MTP texture mappings\n"
      << "  --dat <file.dat>                       Parse DAT, print JSON to stdout\n"
      << "  --dat <file.dat> --output <file.json>  Write DAT JSON to file\n"
      << "  --dat <file.dat> --filter <model>      Include only entries matching model name\n"
      << "  --dat <file.dat> --text                Plain-text output instead of JSON\n"
      << "  --terrain <file.lmp/.ctr>              Parse terrain structure\n"
      << "  --tex <file.tex>                       Parse TEX texture and print "
         "info\n"
      << "  --tex <file.tex> --export-tga <dir>    Export TEX images as TGA "
         "files to dir\n"
      << "  --graph <file.dat>                     Parse navigation graph .dat "
         "file\n"
      << "  --verify-level --level N [--level N ...] Verify levels: compare\n"
      << "      objects.qvm (ground truth) vs igi1ed.log (editor output)\n"
      << "      --skip-launch          Skip launching editor; use existing log\n"
      << "      --timeout <sec>        Kill editor after N seconds (0=wait forever)\n"
      << "      --game-path <path>     IGI1 install path (default from config)\n"
      << "      --log <path>           Override log file path\n"
      << "      --report-json <file>   Write aggregated JSON report\n"
      << "      --report-md <file>     Write aggregated Markdown report\n"
      << "      --report-dir <dir>     Write per-level JSON+MD reports to dir\n"
      << "      --delay <sec>          Delay between levels in seconds (default: 5)\n";
}

int CLIHandler::Process(int argc, char **argv) {
  std::string argsStr;
  for (int i = 0; i < argc; ++i) {
    if (i > 0)
      argsStr += " ";
    argsStr += argv[i];
  }
  Logger::Get().Log(LogLevel::INFO,
                    "[CLI] Process called with arguments: " + argsStr);

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help") {
      Logger::Get().Log(LogLevel::INFO, "[CLI] Help command requested.");
      PrintHelp();
      return 0;
    } else if (arg == "--run-tests") {
      Logger::Get().Log(LogLevel::INFO, "[CLI] Run tests command requested.");
      return RunAllTests();
    } else if (arg == "--mef" && i + 1 < argc) {
      std::string filepath = argv[++i];
      if (i + 1 < argc && std::string(argv[i + 1]) == "--export-obj" &&
          i + 2 < argc) {
        return ExportMEFToObj(filepath, argv[i + 2]);
      } else if (i + 1 < argc && std::string(argv[i + 1]) == "--export-mef" &&
                 i + 2 < argc) {
        return ExportMEFToAscii(filepath, argv[i + 2]);
      } else if (i + 1 < argc && std::string(argv[i + 1]) == "--export-bundle" &&
                 i + 2 < argc) {
        std::string outDir = argv[i + 2];
        i += 2;
        std::string datPath, texDir;
        while (i + 1 < argc) {
          std::string next = argv[i + 1];
          if (next == "--dat" && i + 2 < argc) {
            datPath = argv[i + 2]; i += 2;
          } else if (next == "--texdir" && i + 2 < argc) {
            texDir = argv[i + 2]; i += 2;
          } else {
            break;
          }
        }
        return ExportMEFBundle(filepath, outDir, datPath, texDir);
      }
      return ParseMEF(filepath);
    } else if (arg == "--mtp" && i + 1 < argc) {
      return ParseMTP(argv[++i]);
    } else if (arg == "--dat" && i + 1 < argc) {
      std::string datPath = argv[++i];
      std::string outPath, modelFilter;
      bool useText = false;
      while (i + 1 < argc) {
        std::string next = argv[i + 1];
        if (next == "--output" && i + 2 < argc) {
          outPath = argv[i + 2]; i += 2;
        } else if (next == "--filter" && i + 2 < argc) {
          modelFilter = argv[i + 2]; i += 2;
        } else if (next == "--text") {
          useText = true; ++i;
        } else {
          break;
        }
      }
      return ParseDAT(datPath, outPath, modelFilter, useText);
    } else if (arg == "--qsc" && i + 1 < argc) {
      std::string inpath = argv[++i];
      if (i + 1 < argc && std::string(argv[i + 1]) == "--compile" &&
          i + 2 < argc) {
        return CompileQSC(inpath, argv[i + 2]);
      } else if (i + 1 < argc && std::string(argv[i + 1]) == "--lex") {
        return LexQSC(inpath);
      } else if (i + 1 < argc && std::string(argv[i + 1]) == "--parse") {
        return ParseQSC(inpath);
      }
    } else if (arg == "--qvm" && i + 1 < argc) {
      std::string inpath = argv[++i];
      if (i + 1 < argc && std::string(argv[i + 1]) == "--decompile" &&
          i + 2 < argc) {
        return ParseQVM(inpath, true, argv[i + 2]);
      } else {
        return ParseQVM(inpath, false, "");
      }
    } else if (arg == "--res" && i + 1 < argc) {
      std::string inpath = argv[++i];
      if (i + 1 < argc && std::string(argv[i + 1]) == "--extract" &&
          i + 3 < argc) {
        return ParseRES(inpath, argv[i + 2], argv[i + 3]);
      } else if (i + 1 < argc && std::string(argv[i + 1]) == "--extract-all" &&
                 i + 2 < argc) {
        return ExtractAllRES(inpath, argv[i + 2]);
      } else {
        return ParseRES(inpath, "", "");
      }
    } else if (arg == "--terrain" && i + 1 < argc) {
      return ParseTerrain(argv[++i]);
    } else if (arg == "--tex" && i + 1 < argc) {
      std::string inpath = argv[++i];
      if (i + 1 < argc && std::string(argv[i + 1]) == "--export-tga" &&
          i + 2 < argc) {
        return ParseTEX(inpath, argv[i + 2]);
      }
      return ParseTEX(inpath, "");
    } else if (arg == "--graph" && i + 1 < argc) {
      return ParseGraph(argv[++i]);
    } else if (arg == "--extract-level" && i + 1 < argc) {
      int levelNo = std::stoi(argv[++i]);
      std::string outDir = (i + 1 < argc)
                               ? argv[++i]
                               : Utils::GetExeDirectory() + "\\levels\\level" +
                                     std::to_string(levelNo);
      return ExtractLevelResources(levelNo, outDir);
    } else if (arg == "--verify-level") {
      VerifyLevelParams params;
      // Consume all remaining arguments that belong to --verify-level
      while (i + 1 < argc) {
        std::string nx = argv[i + 1];
        if (nx == "--level" && i + 2 < argc) {
          params.levels.push_back(std::stoi(argv[i + 2])); i += 2;
        } else if (nx == "--skip-launch") {
          params.skipLaunch = true; ++i;
        } else if (nx == "--timeout" && i + 2 < argc) {
          params.timeout = std::stoi(argv[i + 2]); i += 2;
        } else if (nx == "--game-path" && i + 2 < argc) {
          params.gamePath = argv[i + 2]; i += 2;
        } else if (nx == "--log" && i + 2 < argc) {
          params.logPath = argv[i + 2]; i += 2;
        } else if (nx == "--report-json" && i + 2 < argc) {
          params.reportJson = argv[i + 2]; i += 2;
        } else if (nx == "--report-md" && i + 2 < argc) {
          params.reportMd = argv[i + 2]; i += 2;
        } else if (nx == "--report-dir" && i + 2 < argc) {
          params.reportDir = argv[i + 2]; i += 2;
        } else if (nx == "--delay" && i + 2 < argc) {
          params.delay = std::stoi(argv[i + 2]); i += 2;
        } else {
          // Bare integer treated as a level number (backwards-compat: --verify-level 1)
          try { params.levels.push_back(std::stoi(nx)); ++i; }
          catch (...) { break; }
        }
      }
      if (params.levels.empty()) {
        std::cerr << "[VerifyLevel] ERROR: no levels specified. Use --level N.\n";
        return 1;
      }
      return VerifyLevel(params);
    }
  }
  Logger::Get().Log(LogLevel::WARNING,
                    "[CLI] Unknown or invalid arguments processed.");
  PrintHelp();
  return 1;
}

int CLIHandler::ParseMEF(const std::string &filepath) {
  Logger::Get().Log(LogLevel::INFO, "[CLI] Parsing MEF file: " + filepath);
  try {
    ParsedGeometry geometry = ParseMefFile(filepath);
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI] Successfully parsed MEF: " + filepath);
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI]   Layout: " + geometry.renderLayout);
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI]   Type: " + std::to_string(geometry.modelType));
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI]   Vertices: " +
                          std::to_string(geometry.vertices.size()));
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI]   Triangles: " +
                          std::to_string(geometry.triangles.size()));
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI]   Bones: " +
                          std::to_string(geometry.bones.size()));
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI]   Attachments: " +
                          std::to_string(geometry.attachments.size()));
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI]   MEF Attachments: " +
                          std::to_string(geometry.mefAttachments.size()));
    Logger::Get().Log(LogLevel::INFO,
        "[CLI]   Portals: " + std::to_string(geometry.portals.size()));
    return 0;
  } catch (const std::exception &e) {
    // Fallback: try parsing as ASCII MEF using MEFParser
    Logger::Get().Log(LogLevel::INFO, "[CLI] Binary MEF parse failed (" +
                                          std::string(e.what()) +
                                          "). Trying ASCII MEF Parser...");
    MEFParser parser;
    auto objects = parser.parse_file(filepath);
    if (!objects.empty()) {
      size_t totalErrors = 0;
      for (const auto &obj : objects) {
        totalErrors += obj.parse_errors.size();
      }
      if (totalErrors > 0) {
        Logger::Get().Log(LogLevel::ERR, "[CLI] ASCII MEF parser found " +
                                             std::to_string(totalErrors) +
                                             " errors.");
        for (const auto &obj : objects) {
          for (const auto &err : obj.parse_errors) {
            Logger::Get().Log(LogLevel::ERR, "[CLI]   " + err);
          }
        }
        return 1;
      }
      Logger::Get().Log(LogLevel::INFO,
                        "[CLI] Successfully parsed ASCII MEF: " + filepath);
      for (const auto &obj : objects) {
        Logger::Get().Log(LogLevel::INFO, "[CLI]   Object: " + obj.name);
        Logger::Get().Log(LogLevel::INFO,
                          "[CLI]     Vertices: " +
                              std::to_string(obj.vertices.size()));
        Logger::Get().Log(LogLevel::INFO,
                          "[CLI]     Normals: " +
                              std::to_string(obj.normals.size()));
        Logger::Get().Log(LogLevel::INFO, "[CLI]     Faces: " +
                                              std::to_string(obj.faces.size()));
        Logger::Get().Log(LogLevel::INFO,
                          "[CLI]     Materials: " +
                              std::to_string(obj.materials.size()));
        std::cout << "[CLI] Successfully parsed ASCII MEF Object: " << obj.name
                  << "\n"
                  << "  Vertices: " << obj.vertices.size() << "\n"
                  << "  Normals: " << obj.normals.size() << "\n"
                  << "  Faces: " << obj.faces.size() << "\n"
                  << "  Materials: " << obj.materials.size() << "\n";
      }
      return 0;
    }
    Logger::Get().Log(LogLevel::ERR,
                      "[CLI] Failed to parse MEF: " + std::string(e.what()));
    return 1;
  }
}

int CLIHandler::ExportMEFToObj(const std::string &filepath,
                               const std::string &outpath) {
  Logger::Get().Log(LogLevel::INFO, "[CLI] Exporting MEF to OBJ: " + filepath +
                                        " -> " + outpath);
  try {
    ParsedGeometry geometry = ParseMefFile(filepath);
    if (MefExporter::ExportToObj(geometry, outpath)) {
      std::cout << "[CLI] Successfully exported MEF to OBJ: " << outpath
                << "\n";
      return 0;
    }
    return 1;
  } catch (const std::exception &e) {
    Logger::Get().Log(LogLevel::ERR,
                      "[CLI] Failed to export MEF: " + std::string(e.what()));
    return 1;
  }
}

int CLIHandler::ExportMEFToAscii(const std::string &filepath,
                                 const std::string &outpath) {
  Logger::Get().Log(LogLevel::INFO, "[CLI] Exporting MEF to ASCII: " +
                                        filepath + " -> " + outpath);
  try {
    ParsedGeometry geometry = ParseMefFile(filepath);
    if (MefExporter::ExportToMefAscii(geometry, outpath)) {
      std::cout << "[CLI] Successfully exported MEF to ASCII: " << outpath
                << "\n";
      return 0;
    }
    return 1;
  } catch (const std::exception &e) {
    Logger::Get().Log(LogLevel::ERR,
                      "[CLI] Failed to export MEF: " + std::string(e.what()));
    return 1;
  }
}

int CLIHandler::ExportMEFBundle(const std::string &filepath,
                                const std::string &outDir,
                                const std::string &datPath,
                                const std::string &texDir) {
  Logger::Get().Log(LogLevel::INFO,
                    "[CLI] Exporting MEF bundle: " + filepath +
                        " -> " + outDir);
  try {
    ParsedGeometry geometry = ParseMefFile(filepath);

    // Derive model stem from the MEF filename (e.g. "014_01_1" from ".../014_01_1.mef")
    std::filesystem::path mefPath(filepath);
    std::string modelStem = mefPath.stem().string();

    if (MefExporter::ExportToObjBundle(geometry, modelStem, outDir,
                                       datPath, texDir)) {
      std::cout << "[CLI] Bundle exported to: "
                << (std::filesystem::path(outDir) / modelStem).string() << "\n";
      return 0;
    }
    return 1;
  } catch (const std::exception &e) {
    Logger::Get().Log(LogLevel::ERR,
                      "[CLI] Failed to export bundle: " +
                          std::string(e.what()));
    return 1;
  }
}

int CLIHandler::ParseQVM(const std::string &filepath, bool decompile,
                         const std::string &outpath) {
  if (decompile) {
    Logger::Get().Log(LogLevel::INFO, "[CLI] Decompiling QVM (native): " +
                                          filepath + " -> " + outpath);
    QVMFile qvm = QVM_Parse(filepath);
    if (!qvm.valid) {
      Logger::Get().Log(LogLevel::ERR,
                        "[CLI] Failed to parse QVM: " + qvm.error);
      return 1;
    }
    if (QVM_Decompile(qvm, outpath)) {
      Logger::Get().Log(LogLevel::INFO,
                        "[CLI] Successfully decompiled QVM to: " + outpath);
      std::cout << "[CLI] Successfully decompiled QVM to: " << outpath << "\n";
      return 0;
    }
    Logger::Get().Log(LogLevel::ERR,
                      "[CLI] Failed to write decompiled QSC: " + outpath);
    return 1;
  } else {
    Logger::Get().Log(LogLevel::INFO, "[CLI] Parsing QVM: " + filepath);
    QVMFile qvm = QVM_Parse(filepath);
    if (qvm.valid) {
      Logger::Get().Log(LogLevel::INFO,
                        "[CLI] Successfully parsed QVM header and strings.");
      Logger::Get().Log(LogLevel::INFO,
                        "[CLI]   Instructions: " +
                            std::to_string(qvm.totalInstructions()));
      Logger::Get().Log(LogLevel::INFO,
                        "[CLI]   Identifiers: " +
                            std::to_string(qvm.identifierCount()));
      for (size_t i = 0; i < qvm.identifiers.size(); ++i) {
        Logger::Get().Log(LogLevel::INFO, "    [" + std::to_string(i) +
                                              "]: " + qvm.identifiers[i]);
      }
      Logger::Get().Log(LogLevel::INFO, "[CLI]   Strings: " +
                                            std::to_string(qvm.stringCount()));
      return 0;
    }
    Logger::Get().Log(LogLevel::ERR, "[CLI] Failed to parse QVM: " + qvm.error);
    return 1;
  }
}

int CLIHandler::LexQSC(const std::string &inpath) {
  Logger::Get().Log(LogLevel::INFO, "[CLI] Lexing QSC: " + inpath);
  std::ifstream f(inpath, std::ios::binary);
  if (!f) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] Failed to open: " + inpath);
    return 1;
  }
  std::string src((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
  qsc::LexResult r = qsc::Lex(src);
  if (!r.ok) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] " + r.error);
    return 1;
  }
  for (const auto &t : r.tokens) {
    std::cout << t.line << ":" << t.col << "\t" << qsc::TokKindName(t.kind)
              << "\t" << t.lexeme;
    if (t.kind == qsc::TokKind::IntLit || t.kind == qsc::TokKind::HexLit) {
      std::cout << "  (int=" << t.int_val << ")";
    } else if (t.kind == qsc::TokKind::FloatLit) {
      std::cout << "  (float=" << t.float_val << ")";
    }
    std::cout << "\n";
  }
  Logger::Get().Log(LogLevel::INFO, "[CLI] Lexed " +
                                        std::to_string(r.tokens.size()) +
                                        " tokens");
  return 0;
}

int CLIHandler::ParseQSC(const std::string &inpath) {
  Logger::Get().Log(LogLevel::INFO, "[CLI] Parsing QSC: " + inpath);
  std::ifstream f(inpath, std::ios::binary);
  if (!f) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] Failed to open: " + inpath);
    return 1;
  }
  std::string src((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
  qsc::LexResult lr = qsc::Lex(src);
  if (!lr.ok) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] " + lr.error);
    return 1;
  }
  qsc::ParseResult pr = qsc::Parse(lr.tokens);
  if (!pr.ok) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] " + pr.error);
    return 1;
  }
  qsc::DumpAst(*pr.program, std::cout);
  Logger::Get().Log(LogLevel::INFO,
                    "[CLI] Parsed " + std::to_string(pr.call_count) +
                        " calls / " + std::to_string(pr.arg_count) + " args");
  return 0;
}

int CLIHandler::CompileQSC(const std::string &inpath,
                           const std::string &outpath) {
  Logger::Get().Log(LogLevel::INFO,
                    "[CLI] Compiling QSC: " + inpath + " -> " + outpath);
  std::ifstream f(inpath, std::ios::binary);
  if (!f) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] Failed to open: " + inpath);
    return 1;
  }
  std::string src((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
  qsc::LexResult lr = qsc::Lex(src);
  if (!lr.ok) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] " + lr.error);
    return 1;
  }
  qsc::ParseResult pr = qsc::Parse(lr.tokens);
  if (!pr.ok) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] " + pr.error);
    return 1;
  }
  std::string err;
  if (!qvm::CompileToFile(*pr.program, outpath, &err)) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] " + err);
    return 1;
  }
  Logger::Get().Log(LogLevel::INFO, "[CLI] Compiled QSC -> " + outpath);
  return 0;
}

int CLIHandler::ParseRES(const std::string &filepath,
                         const std::string &extract_name,
                         const std::string &outpath) {
  if (!extract_name.empty()) {
    Logger::Get().Log(LogLevel::INFO, "[CLI] Extracting '" + extract_name +
                                          "' from " + filepath + " -> " +
                                          outpath);
    std::vector<uint8_t> data = RES_Extract(filepath, extract_name);
    if (!data.empty()) {
      if (File_SaveBinary(outpath.c_str(), data.data(), data.size())) {
        Logger::Get().Log(LogLevel::INFO,
                          "[CLI] Successfully extracted resource.");
        return 0;
      } else {
        Logger::Get().Log(LogLevel::ERR,
                          "[CLI] Failed to save extracted resource to file: " +
                              outpath);
        return 1;
      }
    }
    Logger::Get().Log(
        LogLevel::ERR,
        "[CLI] Failed to extract resource: block not found or empty in " +
            filepath);
    return 1;
  } else {
    Logger::Get().Log(LogLevel::INFO, "[CLI] Parsing RES archive: " + filepath);
    RESFile res = RES_Parse(filepath);
    if (res.valid) {
      Logger::Get().Log(LogLevel::INFO,
                        "[CLI] Successfully parsed RES archive. Found " +
                            std::to_string(res.entries.size()) + " entries.");
      for (const auto &entry : res.entries) {
        Logger::Get().Log(LogLevel::INFO,
                          "[CLI]   - " + entry.name + " (" +
                              std::to_string(entry.data.size()) + " bytes)");
        std::cout << "  - " << entry.name << " (" << entry.data.size()
                  << " bytes)\n";
      }
      return 0;
    }
    Logger::Get().Log(LogLevel::ERR,
                      "[CLI] Failed to parse RES archive: " + res.error);
    return 1;
  }
}

int CLIHandler::ExtractAllRES(const std::string &filepath,
                              const std::string &outdir) {
  Logger::Get().Log(LogLevel::INFO,
                    "[CLI] Extracting ALL from " + filepath + " to " + outdir);
  RESFile res = RES_Parse(filepath);
  if (!res.valid) {
    Logger::Get().Log(LogLevel::ERR,
                      "[CLI] Failed to parse RES archive: " + res.error);
    return 1;
  }

  namespace fs = std::filesystem;
  try {
    fs::create_directories(outdir);
    int successCount = 0;
    for (const auto &entry : res.entries) {
      // Sanitize name: replace colons and other invalid chars
      std::string safeName = entry.name;
      for (char &c : safeName) {
        if (c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' ||
            c == '>' || c == '|') {
          c = '_';
        }
      }

      std::string outpath = outdir + "\\" + safeName;
      // Ensure subdirectories in entry name exist (but only if they are not
      // from sanitized colons) Actually, IGI names like "models\001_01_1.mef"
      // should be preserved as paths but "LOCAL:models" should become
      // "LOCAL_models"

      // Fix: only sanitize chars that are NOT path separators
      fs::create_directories(fs::path(outpath).parent_path());

      if (File_SaveBinary(outpath.c_str(), entry.data.data(),
                          entry.data.size())) {
        successCount++;
      }
    }
    Logger::Get().Log(LogLevel::INFO, "[CLI] Successfully extracted " +
                                          std::to_string(successCount) + " / " +
                                          std::to_string(res.entries.size()) +
                                          " resources.");
    std::cout << "[CLI] Successfully extracted " << successCount
              << " resources to " << outdir << "\n";
    return 0;
  } catch (const std::exception &e) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] Exception during RES extraction: " +
                                         std::string(e.what()));
    return 1;
  }
}

int CLIHandler::ParseDAT(const std::string &filepath, const std::string &outPath,
                         const std::string &modelFilter, bool textMode) {
  Logger::Get().Log(LogLevel::INFO, "[CLI] Parsing DAT file: " + filepath);
  DATFile dat = DAT_Parse(filepath);
  if (!dat.valid) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] Failed to parse DAT: " + dat.error);
    std::cerr << "ERROR: " << dat.error << "\n";
    return 1;
  }

  if (!outPath.empty()) {
    bool ok = textMode ? DAT_WriteReport(dat, outPath, modelFilter)
                       : DAT_WriteJSON(dat, outPath, modelFilter);
    if (!ok) {
      std::cerr << "ERROR: could not write to " << outPath << "\n";
      return 1;
    }
    std::cout << (textMode ? "DAT report" : "DAT JSON") << " written to: " << outPath << "\n";
  } else {
    std::cout << (textMode ? DAT_FormatReport(dat, modelFilter)
                           : DAT_FormatJSON(dat, modelFilter));
  }
  return 0;
}

int CLIHandler::ParseMTP(const std::string &filepath) {
  Logger::Get().Log(LogLevel::INFO, "[CLI] Parsing MTP file: " + filepath);
  MTPFile mtp = MTP_Parse(filepath);
  if (mtp.valid) {
    Logger::Get().Log(LogLevel::INFO, "[CLI] Successfully parsed MTP.");
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI]   Animations: " +
                          std::to_string(mtp.animations.size()));
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI]   Shadows: " + std::to_string(mtp.shadows.size()));
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI]   Models: " + std::to_string(mtp.models.size()));
    Logger::Get().Log(LogLevel::INFO, "[CLI]   Textures: " +
                                          std::to_string(mtp.textures.size()));
    Logger::Get().Log(LogLevel::INFO, "[CLI]   Mappings: " +
                                          std::to_string(mtp.mappings.size()));
    return 0;
  } else {
    Logger::Get().Log(LogLevel::ERR, "[CLI] Failed to parse MTP: " + mtp.error);
    return 1;
  }
}

int CLIHandler::ParseTEX(const std::string &filepath,
                         const std::string &exportDir) {
  Logger::Get().Log(LogLevel::INFO, "[CLI] Parsing TEX file: " + filepath);
  TEXFile tex = TEX_Parse(filepath);
  if (!tex.valid) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] Failed to parse TEX: " + tex.error);
    return 1;
  }
  Logger::Get().Log(LogLevel::INFO,
                    "[CLI] TEX version: " + std::to_string(tex.version) +
                        "  Images: " + std::to_string(tex.images.size()));
  std::cout << "[CLI] TEX v" << tex.version << "  Images: " << tex.images.size()
            << "\n";
  for (size_t i = 0; i < tex.images.size(); ++i) {
    const auto &img = tex.images[i];
    std::string info =
        "  [" + std::to_string(i) + "] " + std::to_string(img.width) + "x" +
        std::to_string(img.height) + " mode=" + std::to_string(img.mode);
    Logger::Get().Log(LogLevel::INFO, "[CLI]" + info);
    std::cout << info << "\n";
  }
  if (!exportDir.empty()) {
    int written = TEX_ExportTGA(tex, filepath, exportDir);
    Logger::Get().Log(LogLevel::INFO, "[CLI] Exported " +
                                          std::to_string(written) +
                                          " TGA file(s) to: " + exportDir);
    std::cout << "[CLI] Exported " << written
              << " TGA file(s) to: " << exportDir << "\n";
  }
  return 0;
}

int CLIHandler::ParseGraph(const std::string &filepath) {
  Logger::Get().Log(LogLevel::INFO, "[CLI] Parsing Graph file: " + filepath);
  GraphFile graph = GRAPH_Parse(filepath);
  if (!graph.valid) {
    Logger::Get().Log(LogLevel::ERR,
                      "[CLI] Failed to parse Graph: " + graph.error);
    return 1;
  }
  Logger::Get().Log(LogLevel::INFO,
                    "[CLI] Graph nodes: " + std::to_string(graph.nodes.size()) +
                        "  edges: " + std::to_string(graph.edges.size()));
  std::cout << "[CLI] Graph nodes: " << graph.nodes.size()
            << "  edges: " << graph.edges.size() << "\n";
  const size_t preview = std::min(graph.nodes.size(), size_t(10));
  for (size_t i = 0; i < preview; ++i) {
    const auto &n = graph.nodes[i];
    std::cout << "  Node " << n.id << "  pos=(" << n.x << ", " << n.y << ", "
              << n.z << ")"
              << "  mat=" << n.material << "  links=[" << n.link1 << ", "
              << n.link2 << "]\n";
  }
  return 0;
}

int CLIHandler::ParseTerrain(const std::string &filepath) {
  Logger::Get().Log(LogLevel::INFO, "[CLI] Parsing Terrain file: " + filepath);

  // Determine type by extension
  std::string ext = filepath.substr(filepath.find_last_of(".") + 1);
  if (ext == "lmp" || ext == "LMP") {
    pics_s pics;
    if (LMP_Load(filepath.c_str(), pics)) {
      Logger::Get().Log(LogLevel::INFO,
                        "[CLI] Successfully loaded Lightmap (LMP).");
      Logger::Get().Log(LogLevel::INFO,
                        "[CLI]   Textures: " + std::to_string(pics.num_pic_));
      Pic_FreePics(pics);
      return 0;
    }
  } else if (ext == "ctr" || ext == "CTR") {
    ctr_s ctr;
    if (CTR_Load(filepath.c_str(), ctr)) {
      Logger::Get().Log(LogLevel::INFO,
                        "[CLI] Successfully loaded Quadtree (CTR).");
      Logger::Get().Log(LogLevel::INFO,
                        "[CLI]   Items: " + std::to_string(ctr.num_item_));
      CTR_Free(ctr);
      return 0;
    }
  } else {
    Logger::Get().Log(LogLevel::ERR, "[CLI] Unknown terrain format: " + ext);
    return 1;
  }

  Logger::Get().Log(LogLevel::ERR,
                    "[CLI] Failed to parse Terrain file: " + filepath);
  return 1;
}

int CLIHandler::ExtractLevelResources(int levelNo, const std::string &outDir) {
  std::string igiPath = Utils::GetIGIRootPath();
  std::string levelPath =
      igiPath + "\\missions\\location0\\level" + std::to_string(levelNo);

  Logger::Get().Log(LogLevel::INFO, "[CLI] Extracting level " +
                                        std::to_string(levelNo) + " resources");
  Logger::Get().Log(LogLevel::INFO, "[CLI] IGI level path: " + levelPath);
  Logger::Get().Log(LogLevel::INFO, "[CLI] Output dir:     " + outDir);

  namespace fs = std::filesystem;
  try {
    fs::create_directories(outDir + "\\models\\level" + std::to_string(levelNo));
    fs::create_directories(outDir + "\\models");
    fs::create_directories(outDir + "\\textures\\level" + std::to_string(levelNo));
    fs::create_directories(outDir + "\\textures");
    fs::create_directories(outDir + "\\terrain");
  } catch (const std::exception &e) {
    Logger::Get().Log(LogLevel::ERR, "[CLI] Failed to create output dirs: " +
                                         std::string(e.what()));
    return 1;
  }

  int totalExtracted = 0;

  // --- Extract models from level1.res (or loose .mef files) ---
  std::string resFile =
      levelPath + "\\models\\level" + std::to_string(levelNo) + ".res";
  if (fs::exists(resFile)) {
    Logger::Get().Log(LogLevel::INFO,
                      "[CLI] Extracting models from: " + resFile);
    RESFile res = RES_Parse(resFile);
    if (res.valid) {
      for (const auto &entry : res.entries) {
        std::string name = entry.name;
        // Strip path prefix (e.g. "LOCAL:models/xxx.mef" -> "xxx.mef")
        size_t slash = name.find_last_of("/\\");
        std::string filename =
            (slash != std::string::npos) ? name.substr(slash + 1) : name;
        // Write to levelN subdir so FindModelFile's per-level search finds it.
        std::string destPath = outDir + "\\models\\level" + std::to_string(levelNo) + "\\" + filename;
        std::ofstream f(destPath, std::ios::binary);
        if (f) {
          f.write(reinterpret_cast<const char *>(entry.data.data()),
                  entry.data.size());
          totalExtracted++;
        }
      }
      Logger::Get().Log(LogLevel::INFO, "[CLI] Extracted " +
                                            std::to_string(totalExtracted) +
                                            " models from RES");
    } else {
      Logger::Get().Log(LogLevel::WARNING,
                        "[CLI] Could not parse RES: " + res.error);
    }
  } else {
    // Fallback: copy loose .mef files
    std::string modelsDir = levelPath + "\\models";
    if (fs::exists(modelsDir)) {
      for (const auto &entry : fs::directory_iterator(modelsDir)) {
        if (entry.path().extension() == ".mef") {
          std::string dest =
              outDir + "\\models\\level" + std::to_string(levelNo) + "\\" + entry.path().filename().string();
          fs::copy_file(entry.path(), dest,
                        fs::copy_options::overwrite_existing);
          totalExtracted++;
        }
      }
      Logger::Get().Log(LogLevel::INFO, "[CLI] Copied " +
                                            std::to_string(totalExtracted) +
                                            " loose MEF files");
    }
  }

  // --- Extract textures: loose .tex files OR from textures .res archive ---
  int texCount = 0;
  std::string texDir = levelPath + "\\textures";
  // Prefer a per-level subdirectory so FindTextureFile's levelN search works.
  std::string texOutDir = outDir + "\\textures\\level" + std::to_string(levelNo);
  fs::create_directories(texOutDir);

  if (fs::exists(texDir)) {
    // Try .res archive first (e.g. level6/textures/level6.res)
    std::string texResFile = texDir + "\\level" + std::to_string(levelNo) + ".res";
    if (fs::exists(texResFile)) {
      RESFile texRes = RES_Parse(texResFile);
      if (texRes.valid) {
        for (const auto& entry : texRes.entries) {
          size_t slash = entry.name.find_last_of("/\\");
          std::string filename = (slash != std::string::npos) ? entry.name.substr(slash + 1) : entry.name;
          if (filename.size() < 4) continue;
          std::string ext = filename.substr(filename.size() - 4);
          for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
          if (ext != ".tex") continue;
          std::string destPath = texOutDir + "\\" + filename;
          std::ofstream f(destPath, std::ios::binary);
          if (f) { f.write(reinterpret_cast<const char*>(entry.data.data()), entry.data.size()); texCount++; }
        }
        Logger::Get().Log(LogLevel::INFO, "[CLI] Extracted " + std::to_string(texCount) + " TEX files from " + texResFile);
      }
    } else {
      // Fallback: copy loose .tex files
      for (const auto& entry : fs::directory_iterator(texDir)) {
        if (entry.path().extension() == ".tex") {
          std::string dest = texOutDir + "\\" + entry.path().filename().string();
          fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
          texCount++;
        }
      }
      Logger::Get().Log(LogLevel::INFO, "[CLI] Copied " + std::to_string(texCount) + " TEX files");
    }
  }

  // --- Copy terrain files ---
  int terrainCount = 0;
  std::string terrainDir = levelPath + "\\terrain";
  if (fs::exists(terrainDir)) {
    for (const auto &entry : fs::directory_iterator(terrainDir)) {
      std::string dest =
          outDir + "\\terrain\\" + entry.path().filename().string();
      fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
      terrainCount++;
    }
    Logger::Get().Log(LogLevel::INFO, "[CLI] Copied " +
                                          std::to_string(terrainCount) +
                                          " terrain files");
  }

  std::cout << "[ExtractLevel] Level " << levelNo
            << ": models=" << totalExtracted << " textures=" << texCount
            << " terrain=" << terrainCount << "\n";
  return (totalExtracted + texCount + terrainCount > 0) ? 0 : 1;
}
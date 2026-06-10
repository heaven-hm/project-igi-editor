#include "pch.h"
#include "cmd_dat.h"
#include "dat_parser.h"
#include "mtp_parser.h"
#include <filesystem>
#include <set>

static void print_usage()
{
    std::cerr <<
        "Usage:\n"
        "  gconv dat info <file.dat>\n"
        "  gconv dat export <file.dat> [-o <out.json>] [--filter <model>] [--text]\n"
        "  gconv dat to-mtp <file.dat> [-o <out.mtp>]    (native, no external tool)\n"
        "  gconv mtp to-dat <file.mtp> [-o <out.dat>]\n";
}

static const char* opt_val(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc - 1; ++i)
        if (strcmp(argv[i], name) == 0)
            return argv[i + 1];
    return nullptr;
}

static bool has_flag(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], name) == 0)
            return true;
    return false;
}

int cmd_dat(int argc, char** argv)
{
    // argv[0] = "dat", argv[1] = subcommand
    if (argc < 2)
    {
        print_usage();
        return 1;
    }

    std::string sub = argv[1];

    // ── info ─────────────────────────────────────────────────────────────────
    if (sub == "info")
    {
        if (argc < 3)
        {
            std::cerr << "dat info: missing <file.dat>\n";
            return 1;
        }
        std::string path = argv[2];
        if (!std::filesystem::exists(path))
        {
            std::cerr << "dat info: file not found: " << path << "\n";
            return 2;
        }
        DATFile dat = DAT_Parse(path);
        if (!dat.valid)
        {
            std::cerr << "dat info: parse error: " << dat.error << "\n";
            return 3;
        }
        std::cout << "file:            " << path << "\n";
        std::cout << "models:          " << dat.models.size() << "\n";
        std::cout << "declared_models: " << dat.declaredModelCount << "\n";
        std::cout << "textures:        " << dat.allTextures.size() << "\n";
        std::cout << "declared_tex:    " << dat.declaredTextureCount << "\n";
        return 0;
    }

    // ── export ────────────────────────────────────────────────────────────────
    if (sub == "export")
    {
        if (argc < 3)
        {
            std::cerr << "dat export: missing <file.dat>\n";
            return 1;
        }
        std::string path = argv[2];
        if (!std::filesystem::exists(path))
        {
            std::cerr << "dat export: file not found: " << path << "\n";
            return 2;
        }
        DATFile dat = DAT_Parse(path);
        if (!dat.valid)
        {
            std::cerr << "dat export: parse error: " << dat.error << "\n";
            return 3;
        }

        const char* out_path   = opt_val(argc, argv, "-o");
        const char* filter_str = opt_val(argc, argv, "--filter");
        bool text_mode         = has_flag(argc, argv, "--text");

        std::string filter = filter_str ? filter_str : "";

        if (out_path)
        {
            bool ok = text_mode ? DAT_WriteReport(dat, out_path, filter)
                                : DAT_WriteJSON(dat, out_path, filter);
            if (!ok)
            {
                std::cerr << "dat export: failed to write: " << out_path << "\n";
                return 4;
            }
            std::cout << "Written to: " << out_path << "\n";
        }
        else
        {
            std::cout << (text_mode ? DAT_FormatReport(dat, filter)
                                    : DAT_FormatJSON(dat, filter));
        }
        return 0;
    }

    // ── to-mtp ────────────────────────────────────────────────────────────────
    if (sub == "to-mtp")
    {
        if (argc < 3)
        {
            std::cerr << "dat to-mtp: missing <file.dat>\n";
            return 1;
        }
        std::string dat_path = argv[2];
        if (!std::filesystem::exists(dat_path))
        {
            std::cerr << "dat to-mtp: file not found: " << dat_path << "\n";
            return 2;
        }
        DATFile dat = DAT_Parse(dat_path);
        if (!dat.valid)
        {
            std::cerr << "dat to-mtp: parse error: " << dat.error << "\n";
            return 3;
        }

        const char* out_arg = opt_val(argc, argv, "-o");
        std::string out_mtp = out_arg ? std::string(out_arg)
                                      : std::filesystem::path(dat_path).replace_extension(".mtp").string();

        // Build MTPModelTexture list from all DAT models.
        std::vector<MTPModelTexture> mappings;
        mappings.reserve(dat.models.size());
        for (const auto& m : dat.models)
            mappings.push_back({m.modelName, m.textures});

        // Collect textures present in allTextures but not in any model's texture list
        // (e.g. the literal "0" manifest entry that some DAT files include).
        std::set<std::string> modelTexSet;
        for (const auto& m : dat.models)
            for (const auto& t : m.textures)
                modelTexSet.insert(t);
        std::vector<std::string> extraTextures;
        for (const auto& t : dat.allTextures)
            if (modelTexSet.find(t) == modelTexSet.end())
                extraTextures.push_back(t);

        std::string merr;
        if (!MTP_Generate(out_mtp, mappings, merr, extraTextures))
        {
            std::cerr << "dat to-mtp: " << merr << "\n";
            return 4;
        }
        std::cout << "Generated MTP: " << out_mtp << "\n";
        return 0;
    }

    std::cerr << "dat: unknown subcommand '" << sub << "'\n";
    print_usage();
    return 1;
}

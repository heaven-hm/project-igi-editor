#include "pch.h"
#include "cmd_mtp.h"
#include "mtp_parser.h"
#include "dat_parser.h"

static void print_usage()
{
    std::cerr <<
        "Usage:\n"
        "  gconv mtp dump <input.mtp> [-o <output.json>]\n"
        "  gconv mtp info <input.mtp>\n"
        "  gconv mtp to-dat <input.mtp> [-o <out.dat>]\n"
        "  gconv mtp repair <input.mtp>              (sync VNAM/GTT counts)\n"
        "  gconv mtp sync <input.mtp> <input.dat>    (add DAT models missing from MTP)\n";
}

// Minimal JSON string escaping
static std::string json_str(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s)
    {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    out += '"';
    return out;
}

// Emit a JSON array of strings
static std::string json_str_array(const std::vector<std::string>& v)
{
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (i) out += ",";
        out += json_str(v[i]);
    }
    out += "]";
    return out;
}

static void write_json(std::ostream& os, const MTPFile& mtp)
{
    if (!mtp.valid ||
        (mtp.animations.empty() && mtp.shadows.empty() &&
         mtp.models.empty()     && mtp.textures.empty() &&
         mtp.mappings.empty()))
    {
        os << "{}\n";
        return;
    }

    os << "{\n";
    os << "  \"animations\": " << json_str_array(mtp.animations) << ",\n";
    os << "  \"shadows\": "    << json_str_array(mtp.shadows)    << ",\n";
    os << "  \"models\": "     << json_str_array(mtp.models)     << ",\n";
    os << "  \"textures\": "   << json_str_array(mtp.textures)   << ",\n";

    os << "  \"mappings\": [";
    for (size_t i = 0; i < mtp.mappings.size(); ++i)
    {
        if (i) os << ",";
        os << "\n    {\"model\": " << json_str(mtp.mappings[i].modelName)
           << ", \"textures\": "  << json_str_array(mtp.mappings[i].textureNames) << "}";
    }
    os << "\n  ]\n";
    os << "}\n";
}

int cmd_mtp(int argc, char** argv)
{
    if (argc < 2)
    {
        print_usage();
        return 1;
    }

    std::string sub = argv[1];

    // ── dump ──────────────────────────────────────────────────────────────────
    if (sub == "dump")
    {
        if (argc < 3)
        {
            std::cerr << "mtp dump: missing <input.mtp>\n";
            return 1;
        }
        std::string path = argv[2];

        const char* out_path = nullptr;
        for (int i = 3; i < argc - 1; ++i)
            if (strcmp(argv[i], "-o") == 0)
                out_path = argv[i + 1];

        if (!std::filesystem::exists(path))
        {
            std::cerr << "mtp dump: file not found: " << path << "\n";
            return 2;
        }

        MTPFile mtp = MTP_Parse(path);
        if (!mtp.valid)
        {
            std::cerr << "mtp dump: parse error: " << mtp.error << "\n";
            return 3;
        }

        if (out_path)
        {
            std::ofstream ofs(out_path);
            if (!ofs)
            {
                std::cerr << "mtp dump: cannot write: " << out_path << "\n";
                return 4;
            }
            write_json(ofs, mtp);
        }
        else
        {
            write_json(std::cout, mtp);
        }
        return 0;
    }

    // ── info ──────────────────────────────────────────────────────────────────
    if (sub == "info")
    {
        if (argc < 3)
        {
            std::cerr << "mtp info: missing <input.mtp>\n";
            return 1;
        }
        std::string path = argv[2];

        if (!std::filesystem::exists(path))
        {
            std::cerr << "mtp info: file not found: " << path << "\n";
            return 2;
        }

        MTPFile mtp = MTP_Parse(path);
        if (!mtp.valid)
        {
            std::cerr << "mtp info: parse error: " << mtp.error << "\n";
            return 3;
        }

        std::cout << "File:       " << path << "\n";
        std::cout << "Animations: " << mtp.animations.size() << "\n";
        std::cout << "Shadows:    " << mtp.shadows.size()    << "\n";
        std::cout << "Models:     " << mtp.models.size()     << "\n";
        std::cout << "Textures:   " << mtp.textures.size()   << "\n";
        std::cout << "Mappings:   " << mtp.mappings.size()   << "\n";

        if (!mtp.models.empty())
        {
            std::cout << "\nModel list:\n";
            for (const auto& m : mtp.models)
                std::cout << "  " << m << "\n";
        }
        if (!mtp.textures.empty())
        {
            std::cout << "\nTexture list:\n";
            for (const auto& t : mtp.textures)
                std::cout << "  " << t << "\n";
        }
        return 0;
    }

    // ── to-dat ────────────────────────────────────────────────────────────────
    if (sub == "to-dat")
    {
        if (argc < 3)
        {
            std::cerr << "mtp to-dat: missing <input.mtp>\n";
            return 1;
        }
        std::string path = argv[2];

        const char* out_arg = nullptr;
        for (int i = 3; i < argc - 1; ++i)
            if (strcmp(argv[i], "-o") == 0) { out_arg = argv[i + 1]; break; }

        if (!std::filesystem::exists(path))
        {
            std::cerr << "mtp to-dat: file not found: " << path << "\n";
            return 2;
        }

        MTPFile mtp = MTP_Parse(path);
        if (!mtp.valid)
        {
            std::cerr << "mtp to-dat: parse error: " << mtp.error << "\n";
            return 3;
        }

        // Build DAT from MTP mappings (mappings already include "waypoint" if present)
        DATFile dat;
        dat.valid = true;
        for (const auto& m : mtp.mappings)
        {
            DATModelEntry e;
            e.modelName = m.modelName;
            e.textures  = m.textureNames;
            dat.models.push_back(e);
        }
        dat.allTextures           = mtp.textures;
        dat.declaredModelCount    = (int)dat.models.size();
        dat.declaredTextureCount  = (int)dat.allTextures.size();

        std::string out_dat = out_arg ? std::string(out_arg)
                                      : std::filesystem::path(path).replace_extension(".dat").string();
        std::string err;
        if (!DAT_WriteNative(dat, out_dat, err))
        {
            std::cerr << "mtp to-dat: write failed: " << err << "\n";
            return 4;
        }
        std::cout << "Wrote DAT: " << out_dat << "\n";
        return 0;
    }

    // ── repair ────────────────────────────────────────────────────────────────
    if (sub == "repair")
    {
        if (argc < 3)
        {
            std::cerr << "mtp repair: missing <input.mtp>\n";
            return 1;
        }
        std::string path = argv[2];
        if (!std::filesystem::exists(path))
        {
            std::cerr << "mtp repair: file not found: " << path << "\n";
            return 2;
        }
        // Use MTP_AddModel with the first model already in the file — the idempotent
        // path now syncs VNAM/GTT counts to match MODS/TEXF without changing content.
        MTPFile mtp = MTP_Parse(path);
        if (!mtp.valid || mtp.models.empty())
        {
            std::cerr << "mtp repair: parse error or empty MTP: "
                      << (mtp.valid ? "no models" : mtp.error) << "\n";
            return 3;
        }
        // Get the textures for the first model from its INST mapping.
        std::vector<std::string> firstTextures;
        if (!mtp.mappings.empty())
            firstTextures = mtp.mappings[0].textureNames;

        std::string err;
        if (!MTP_AddModel(path, path, mtp.models[0], firstTextures, err))
        {
            std::cerr << "mtp repair: " << err << "\n";
            return 4;
        }
        // Verify result.
        MTPFile fixed = MTP_Parse(path);
        std::cout << "Repaired: " << path << "\n"
                  << "  Models=" << fixed.models.size()
                  << " Textures=" << fixed.textures.size()
                  << " Mappings=" << fixed.mappings.size() << "\n";
        return 0;
    }

    // ── sync ──────────────────────────────────────────────────────────────────
    if (sub == "sync")
    {
        if (argc < 4)
        {
            std::cerr << "mtp sync: usage: mtp sync <file.mtp> <file.dat>\n";
            return 1;
        }
        std::string mtpPath = argv[2];
        std::string datPath = argv[3];
        if (!std::filesystem::exists(mtpPath))
        {
            std::cerr << "mtp sync: mtp not found: " << mtpPath << "\n";
            return 2;
        }
        if (!std::filesystem::exists(datPath))
        {
            std::cerr << "mtp sync: dat not found: " << datPath << "\n";
            return 2;
        }
        DATFile dat = DAT_Parse(datPath);
        if (!dat.valid)
        {
            std::cerr << "mtp sync: dat parse error: " << dat.error << "\n";
            return 3;
        }
        int added = 0, confirmed = 0;
        for (const auto& m : dat.models)
        {
            std::string err;
            if (!MTP_AddModel(mtpPath, mtpPath, m.modelName, m.textures, err))
            {
                std::cerr << "mtp sync: MTP_AddModel failed for " << m.modelName << ": " << err << "\n";
                return 4;
            }
            // Re-parse to check if it was newly added or already present.
            ++confirmed;
        }
        MTPFile result = MTP_Parse(mtpPath);
        std::cout << "Synced " << mtpPath << "\n"
                  << "  Models=" << result.models.size()
                  << " Textures=" << result.textures.size()
                  << " (processed " << confirmed << " DAT entries)\n";
        return 0;
    }

    std::cerr << "mtp: unknown subcommand '" << sub << "'\n";
    print_usage();
    return 1;
}

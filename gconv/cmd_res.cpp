#include "pch.h"
#include "cmd_res.h"
#include "res_parser.h"
#include "res_compiler.h"
#include <filesystem>

static void print_usage()
{
    std::cerr <<
        "Usage:\n"
        "  gconv res list <input.res>\n"
        "  gconv res extract <input.res> -o <output_dir>\n"
        "  gconv res extract <input.res> --file <name> -o <output_dir>\n"
        "  gconv res compile <file.qsc>\n"
        "  gconv res pack <dir> <out.res>\n"
        "  gconv res unpack <file.res> <dir>\n"
        "  gconv res append <input.res> <file1> [file2...] -o <out.res> [--prefix LOCAL:textures/]\n";
}

// Return the value of a named option (e.g. "-o", "--file"), or nullptr if absent.
static const char* opt_val(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc - 1; ++i)
        if (strcmp(argv[i], name) == 0)
            return argv[i + 1];
    return nullptr;
}

int cmd_res(int argc, char** argv)
{
    // argv[0] = "res", argv[1] = subcommand
    if (argc < 2)
    {
        print_usage();
        return 1;
    }

    std::string sub = argv[1];

    // ── list ──────────────────────────────────────────────────────────────────
    if (sub == "list")
    {
        if (argc < 3)
        {
            std::cerr << "res list: missing <input.res>\n";
            return 1;
        }
        std::string path = argv[2];
        std::string err;
        bool ok = RES_ForEachEntry(path, [](const std::string& name, const uint8_t*, size_t) {
            std::cout << name << "\n";
        }, err);

        if (!ok)
        {
            std::cerr << "res list: " << err << "\n";
            // Distinguish file-not-found from parse error
            if (!std::filesystem::exists(path))
                return 2;
            return 3;
        }
        return 0;
    }

    // ── extract ───────────────────────────────────────────────────────────────
    if (sub == "extract")
    {
        if (argc < 3)
        {
            std::cerr << "res extract: missing <input.res>\n";
            return 1;
        }
        std::string path = argv[2];

        const char* out_dir  = opt_val(argc, argv, "-o");
        const char* only_file = opt_val(argc, argv, "--file");

        if (!out_dir)
        {
            std::cerr << "res extract: missing -o <output_dir>\n";
            return 1;
        }

        if (!std::filesystem::exists(path))
        {
            std::cerr << "res extract: file not found: " << path << "\n";
            return 2;
        }

        // Create output directory if it doesn't exist
        std::error_code ec;
        std::filesystem::create_directories(out_dir, ec);
        if (ec)
        {
            std::cerr << "res extract: cannot create output dir: " << ec.message() << "\n";
            return 4;
        }

        int extracted = 0;
        std::string err;
        bool ok = RES_ForEachEntry(path,
            [&](const std::string& name, const uint8_t* data, size_t size) {
                if (only_file && name != only_file)
                    return;

                // Use the base name of the entry so nested paths don't create
                // unexpected subdirectories in the output dir.
                std::filesystem::path entry_path(name);
                std::filesystem::path out_path =
                    std::filesystem::path(out_dir) / entry_path.filename();

                std::ofstream ofs(out_path, std::ios::binary);
                if (!ofs)
                {
                    std::cerr << "res extract: cannot write: " << out_path << "\n";
                    return;
                }
                ofs.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
                ++extracted;
            }, err);

        if (!ok)
        {
            std::cerr << "res extract: " << err << "\n";
            return 3;
        }

        std::cout << "Extracted " << extracted << " file(s) to " << out_dir << "\n";
        return 0;
    }

    // ── compile ───────────────────────────────────────────────────────────────
    if (sub == "compile")
    {
        if (argc < 3)
        {
            std::cerr << "res compile: missing <file.qsc>\n";
            return 1;
        }
        std::string qsc_path = argv[2];
        if (!std::filesystem::exists(qsc_path))
        {
            std::cerr << "res compile: file not found: " << qsc_path << "\n";
            return 2;
        }
        std::string err;
        if (!RES_Compile(qsc_path, err))
        {
            std::cerr << "res compile: " << err << "\n";
            return 3;
        }
        std::cout << "res compile: success\n";
        return 0;
    }

    // ── pack ──────────────────────────────────────────────────────────────────
    if (sub == "pack")
    {
        if (argc < 4)
        {
            std::cerr << "res pack: usage: gconv res pack <dir> <out.res>\n";
            return 1;
        }
        std::string dir     = argv[2];
        std::string out_res = argv[3];
        // Normalize separators for RES_GenerateQSC
        for (auto& c : out_res) if (c == '\\') c = '/';

        if (!std::filesystem::is_directory(dir))
        {
            std::cerr << "res pack: not a directory: " << dir << "\n";
            return 2;
        }

        std::string qsc_path = (std::filesystem::path(dir) / "resource.qsc").string();
        std::string err;
        if (!RES_GenerateQSC(dir, qsc_path, out_res, err))
        {
            std::cerr << "res pack (generate qsc): " << err << "\n";
            return 3;
        }
        if (!RES_Compile(qsc_path, err))
        {
            std::cerr << "res pack (compile): " << err << "\n";
            return 3;
        }
        std::cout << "res pack: packed to " << out_res << "\n";
        return 0;
    }

    // ── unpack ────────────────────────────────────────────────────────────────
    if (sub == "unpack")
    {
        if (argc < 4)
        {
            std::cerr << "res unpack: usage: gconv res unpack <file.res> <dir>\n";
            return 1;
        }
        std::string res_path = argv[2];
        std::string out_dir  = argv[3];

        if (!std::filesystem::exists(res_path))
        {
            std::cerr << "res unpack: file not found: " << res_path << "\n";
            return 2;
        }

        std::error_code ec;
        std::filesystem::create_directories(out_dir, ec);
        if (ec)
        {
            std::cerr << "res unpack: cannot create dir: " << ec.message() << "\n";
            return 4;
        }

        int extracted = 0;
        std::string err;
        bool ok = RES_ForEachEntry(res_path,
            [&](const std::string& name, const uint8_t* data, size_t size) {
                std::filesystem::path entry_path(name);
                std::filesystem::path out_path =
                    std::filesystem::path(out_dir) / entry_path.filename();

                std::ofstream ofs(out_path, std::ios::binary);
                if (!ofs) { std::cerr << "res unpack: cannot write: " << out_path << "\n"; return; }
                ofs.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
                ++extracted;
            }, err);

        if (!ok)
        {
            std::cerr << "res unpack: " << err << "\n";
            return 3;
        }
        std::cout << "Unpacked " << extracted << " file(s) to " << out_dir << "\n";
        return 0;
    }

    // ── append ────────────────────────────────────────────────────────────────
    if (sub == "append")
    {
        // gconv res append <input.res> <file1> [file2...] -o <out.res> [--prefix <p>]
        if (argc < 5)
        {
            std::cerr << "res append: usage: gconv res append <input.res> <file1> [file2 ...] -o <out.res> [--prefix LOCAL:textures/]\n";
            return 1;
        }
        std::string src_res = argv[2];

        const char* out_res  = opt_val(argc, argv, "-o");
        const char* prefix_c = opt_val(argc, argv, "--prefix");
        std::string prefix = prefix_c ? prefix_c : "";

        if (!out_res)
        {
            std::cerr << "res append: missing -o <out.res>\n";
            return 1;
        }
        if (!std::filesystem::exists(src_res))
        {
            std::cerr << "res append: file not found: " << src_res << "\n";
            return 2;
        }

        // Collect input files: argv[3..] until we hit "-o" or "--prefix"
        std::vector<std::string> input_files;
        for (int i = 3; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a == "-o" || a == "--prefix") { ++i; continue; }
            input_files.push_back(a);
        }
        if (input_files.empty())
        {
            std::cerr << "res append: no input files specified\n";
            return 1;
        }

        std::vector<RESEntry> new_entries;
        for (const auto& fpath : input_files)
        {
            if (!std::filesystem::exists(fpath))
            {
                std::cerr << "res append: file not found: " << fpath << "\n";
                return 2;
            }
            std::ifstream f(fpath, std::ios::binary | std::ios::ate);
            if (!f) { std::cerr << "res append: cannot read: " << fpath << "\n"; return 4; }
            std::streamsize sz = f.tellg(); f.seekg(0);
            RESEntry e;
            e.name = prefix + std::filesystem::path(fpath).filename().string();
            e.data.resize(sz);
            f.read(reinterpret_cast<char*>(e.data.data()), sz);
            new_entries.push_back(std::move(e));
        }

        std::string err;
        if (!RES_StreamAppend(src_res, new_entries, out_res, err))
        {
            std::cerr << "res append: " << err << "\n";
            return 3;
        }
        std::cout << "res append: appended " << new_entries.size() << " file(s) to " << out_res << "\n";
        return 0;
    }

    std::cerr << "res: unknown subcommand '" << sub << "'\n";
    print_usage();
    return 1;
}

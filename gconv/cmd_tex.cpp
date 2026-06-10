#include "pch.h"
#include "cmd_tex.h"
#include "tex_parser.h"

namespace fs = std::filesystem;

static void print_tex_help()
{
    std::cout <<
        "Usage: gconv tex <subcommand> [options]\n"
        "\n"
        "Subcommands:\n"
        "  decode <input.tex|.spr|.pic> -o <output_dir>\n"
        "  decode <folder/> -o <output_dir> --batch\n"
        "  info   <input.tex|.spr|.pic>\n"
        "\n"
        "Exit codes: 0=success 1=bad args 2=file not found 3=parse error 4=write error\n";
}

static int do_tex_decode(const std::string& input, const std::string& outdir)
{
    if (!fs::exists(input))
    {
        std::cerr << "tex: file not found: " << input << "\n";
        return 2;
    }

    TEXFile tex = TEX_Parse(input);
    if (!tex.valid)
    {
        std::cerr << "tex: parse error: " << tex.error << "\n";
        return 3;
    }

    if (!fs::exists(outdir))
    {
        std::error_code ec;
        fs::create_directories(outdir, ec);
        if (ec)
        {
            std::cerr << "tex: cannot create output dir: " << outdir << " (" << ec.message() << ")\n";
            return 4;
        }
    }

    int written = TEX_ExportTGA(tex, input, outdir);
    if (written <= 0)
    {
        std::cerr << "tex: failed to write TGA files to: " << outdir << "\n";
        return 4;
    }

    std::cout << "tex: wrote " << written << " TGA file(s) to " << outdir << "\n";
    return 0;
}

static int do_tex_info(const std::string& input)
{
    if (!fs::exists(input))
    {
        std::cerr << "tex: file not found: " << input << "\n";
        return 2;
    }

    TEXFile tex = TEX_Parse(input);
    if (!tex.valid)
    {
        std::cerr << "tex: parse error: " << tex.error << "\n";
        return 3;
    }

    std::cout << "file:    " << input << "\n";
    std::cout << "version: " << tex.version << "\n";
    std::cout << "images:  " << tex.images.size() << "\n";
    for (size_t i = 0; i < tex.images.size(); ++i)
    {
        const TEXImage& img = tex.images[i];
        const char* mode_str = (img.mode == 2) ? "RGB565" : "ARGB8888";
        std::cout << "  [" << i << "] " << img.width << "x" << img.height
                  << " mode=" << img.mode << " (" << mode_str << ")"
                  << " bytes=" << img.pixels.size() << "\n";
    }
    return 0;
}

int cmd_tex(int argc, char** argv)
{
    // argv[0] = "tex", argv[1] = subcommand
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")
    {
        print_tex_help();
        return (argc < 2) ? 1 : 0;
    }

    std::string subcmd = argv[1];

    if (subcmd == "info")
    {
        if (argc < 3)
        {
            std::cerr << "tex info: missing input file\n";
            return 1;
        }
        return do_tex_info(argv[2]);
    }

    if (subcmd == "decode")
    {
        if (argc < 3)
        {
            std::cerr << "tex decode: missing input\n";
            return 1;
        }

        // Find -o <outdir>
        std::string outdir;
        for (int i = 3; i < argc - 1; ++i)
        {
            if (std::string(argv[i]) == "-o")
            {
                outdir = argv[i + 1];
                break;
            }
        }
        if (outdir.empty())
        {
            std::cerr << "tex decode: missing -o <output_dir>\n";
            return 1;
        }

        bool batch = false;
        for (int i = 3; i < argc; ++i)
        {
            if (std::string(argv[i]) == "--batch")
            {
                batch = true;
                break;
            }
        }

        std::string input = argv[2];

        if (batch)
        {
            if (!fs::is_directory(input))
            {
                std::cerr << "tex decode --batch: input is not a directory: " << input << "\n";
                return 2;
            }

            bool any_failed = false;
            for (const auto& entry : fs::directory_iterator(input))
            {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                // Lowercase the extension for comparison
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".tex" && ext != ".spr" && ext != ".pic") continue;

                int rc = do_tex_decode(entry.path().string(), outdir);
                if (rc != 0)
                {
                    std::cerr << "tex: error processing " << entry.path().filename().string() << " (rc=" << rc << ")\n";
                    any_failed = true;
                }
            }
            return any_failed ? 3 : 0;
        }
        else
        {
            return do_tex_decode(input, outdir);
        }
    }

    std::cerr << "tex: unknown subcommand '" << subcmd << "'\n";
    std::cerr << "Run 'gconv tex --help' for usage.\n";
    return 1;
}

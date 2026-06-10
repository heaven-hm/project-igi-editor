#include "pch.h"
#include "cmd_tex.h"
#include "tex_parser.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/tinygltf/stb_image.h"
// stb_image_write is defined in cmd_fnt.cpp; just include header here
#include "../../third_party/tinygltf/stb_image_write.h"

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
        "  to-png <input> [-o <out.png>] [--resize <W> <H>]\n"
        "  to-tga <input> [-o <out.tga>] [--resize <W> <H>]\n"
        "\n"
        "  <input> for to-png/to-tga: .tex .spr .pic .tga .png .bmp .jpg .jpeg\n"
        "  -o is optional; default: same directory as input, same base name\n"
        "  --resize W H: scale the output to W x H pixels\n"
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

    if (subcmd == "to-png" || subcmd == "to-tga")
    {
        if (argc < 3)
        {
            std::cerr << "tex " << subcmd << ": missing input file\n";
            return 1;
        }
        std::string input = argv[2];

        // Parse -o, --resize W H
        std::string outpath;
        int resize_w = 0, resize_h = 0;
        for (int i = 3; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a == "-o" && i + 1 < argc)      { outpath = argv[++i]; }
            else if (a == "--resize" && i + 2 < argc) {
                try { resize_w = std::stoi(argv[i+1]); resize_h = std::stoi(argv[i+2]); i += 2; }
                catch (...) { std::cerr << "tex " << subcmd << ": --resize needs two integers\n"; return 1; }
            }
        }

        if (!fs::exists(input))
        {
            std::cerr << "tex " << subcmd << ": file not found: " << input << "\n";
            return 2;
        }

        // Default output path: same dir, same stem, new extension
        if (outpath.empty())
        {
            fs::path p(input);
            std::string new_ext = (subcmd == "to-png") ? ".png" : ".tga";
            outpath = (p.parent_path() / (p.stem().string() + new_ext)).string();
        }

        std::string ext = fs::path(input).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Load pixels from input
        int w = 0, h = 0;
        std::vector<unsigned char> pixels;

        bool is_stb_format = (ext == ".tga" || ext == ".png" || ext == ".bmp" ||
                              ext == ".jpg" || ext == ".jpeg" || ext == ".gif");
        if (is_stb_format)
        {
            int channels;
            unsigned char* data = stbi_load(input.c_str(), &w, &h, &channels, 4);
            if (!data)
            {
                std::cerr << "tex " << subcmd << ": failed to load image: " << input << "\n";
                return 3;
            }
            pixels.assign(data, data + w * h * 4);
            stbi_image_free(data);
        }
        else
        {
            // TEX/SPR/PIC: raw pixels are not RGBA; decode via TGA round-trip
            TEXFile tex = TEX_Parse(input);
            if (!tex.valid)
            {
                std::cerr << "tex " << subcmd << ": parse error: " << tex.error << "\n";
                return 3;
            }
            if (tex.images.empty())
            {
                std::cerr << "tex " << subcmd << ": no images in file\n";
                return 3;
            }
            // Write to a temp TGA then re-load via stb_image to get RGBA8888
            std::string tmp_tga = outpath + ".~tmp.tga";
            if (!TEX_WriteTGA(tmp_tga, tex.images[0]))
            {
                std::cerr << "tex " << subcmd << ": failed to decode TEX pixels\n";
                return 3;
            }
            int channels;
            unsigned char* data = stbi_load(tmp_tga.c_str(), &w, &h, &channels, 4);
            fs::remove(tmp_tga);
            if (!data)
            {
                std::cerr << "tex " << subcmd << ": failed to decode TGA: " << input << "\n";
                return 3;
            }
            pixels.assign(data, data + w * h * 4);
            stbi_image_free(data);
        }

        // Resize if requested (nearest-neighbour)
        if (resize_w > 0 && resize_h > 0 && (resize_w != w || resize_h != h))
        {
            std::vector<unsigned char> resized(resize_w * resize_h * 4);
            float sx = (float)w / resize_w;
            float sy = (float)h / resize_h;
            for (int ry = 0; ry < resize_h; ++ry) {
                for (int rx = 0; rx < resize_w; ++rx) {
                    int px = std::min((int)(rx * sx), w - 1);
                    int py = std::min((int)(ry * sy), h - 1);
                    const unsigned char* s = pixels.data() + (py * w + px) * 4;
                    unsigned char* d = resized.data() + (ry * resize_w + rx) * 4;
                    d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=s[3];
                }
            }
            pixels = std::move(resized);
            w = resize_w; h = resize_h;
        }

        // Write output
        int rc = 0;
        if (subcmd == "to-png")
            rc = stbi_write_png(outpath.c_str(), w, h, 4, pixels.data(), w * 4);
        else
            rc = stbi_write_tga(outpath.c_str(), w, h, 4, pixels.data());

        if (!rc)
        {
            std::cerr << "tex " << subcmd << ": failed to write: " << outpath << "\n";
            return 4;
        }
        std::cout << "tex: converted " << input << " -> " << outpath << "\n";
        return 0;
    }

    std::cerr << "tex: unknown subcommand '" << subcmd << "'\n";
    std::cerr << "Run 'gconv tex --help' for usage.\n";
    return 1;
}

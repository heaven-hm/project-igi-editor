#include "pch.h"
#include "cmd_mef.h"
#include "mef_native.h"
#include "mef_exporter.h"
#include "mef_parser.h"

namespace fs = std::filesystem;

static void print_mef_help()
{
    std::cout <<
        "Usage: gconv mef <subcommand> [options]\n"
        "\n"
        "Subcommands:\n"
        "  export <input.mef> -o <output.obj>\n"
        "  export <folder/> -o <output_dir> --batch\n"
        "  dump   <input.mef> [-o <output.txt>]\n"
        "  info   <input.mef>\n"
        "\n"
        "Exit codes: 0=success 1=bad args 2=file not found 3=parse error 4=write error\n";
}

static int do_mef_export(const std::string& input, const std::string& outpath)
{
    if (!fs::exists(input))
    {
        std::cerr << "mef: file not found: " << input << "\n";
        return 2;
    }

    ParsedGeometry geo;
    try
    {
        geo = ParseMefFile(input);
    }
    catch (const std::exception& e)
    {
        std::cerr << "mef: parse error: " << e.what() << "\n";
        return 3;
    }

    if (!MefExporter::ExportToObj(geo, outpath))
    {
        std::cerr << "mef: failed to write OBJ: " << outpath << "\n";
        return 4;
    }

    std::cout << "mef: exported to " << outpath << "\n";
    return 0;
}

static int do_mef_dump(const std::string& input, const std::string& outpath)
{
    if (!fs::exists(input))
    {
        std::cerr << "mef: file not found: " << input << "\n";
        return 2;
    }

    // MEFParser parses the ASCII .mef format
    MEFParser parser;
    std::vector<MEFObject> objects;
    try
    {
        objects = parser.parse_file(input);
    }
    catch (const std::exception& e)
    {
        std::cerr << "mef: parse error: " << e.what() << "\n";
        return 3;
    }

    std::ostream* out_ptr = &std::cout;
    std::ofstream fout;
    if (!outpath.empty())
    {
        fout.open(outpath);
        if (!fout.is_open())
        {
            std::cerr << "mef: cannot open output file: " << outpath << "\n";
            return 4;
        }
        out_ptr = &fout;
    }

    std::ostream& out = *out_ptr;
    out << "file: " << input << "\n";
    out << "objects: " << objects.size() << "\n\n";

    for (size_t oi = 0; oi < objects.size(); ++oi)
    {
        const MEFObject& obj = objects[oi];
        out << "--- object[" << oi << "]: " << obj.name << " ---\n";
        out << "  vertices:  " << obj.vertices.size() << "\n";
        out << "  normals:   " << obj.normals.size() << "\n";
        out << "  faces:     " << obj.faces.size() << "\n";
        out << "  uvs:       " << obj.uvs.size() << "\n";
        out << "  materials: " << obj.materials.size() << "\n";
        for (size_t mi = 0; mi < obj.materials.size(); ++mi)
        {
            const MEFMaterial& mat = obj.materials[mi];
            out << "    mat[" << mi << "] index=" << mat.index
                << " name=\"" << mat.name << "\""
                << " shininess=" << mat.shininess
                << (mat.has_collision ? " [collision]" : "")
                << "\n";
        }
        if (!obj.parse_errors.empty())
        {
            out << "  parse_errors: " << obj.parse_errors.size() << "\n";
            for (const auto& err : obj.parse_errors)
                out << "    " << err << "\n";
        }
        out << "\n";
    }

    return 0;
}

static int do_mef_info(const std::string& input)
{
    if (!fs::exists(input))
    {
        std::cerr << "mef: file not found: " << input << "\n";
        return 2;
    }

    ParsedGeometry geo;
    try
    {
        geo = ParseMefFile(input);
    }
    catch (const std::exception& e)
    {
        std::cerr << "mef: parse error: " << e.what() << "\n";
        return 3;
    }

    std::cout << "file:            " << input << "\n";
    std::cout << "layout:          " << geo.renderLayout << "\n";
    std::cout << "model_type:      " << geo.modelType << "\n";
    std::cout << "vertices:        " << geo.vertices.size() << "\n";
    std::cout << "triangles:       " << geo.triangles.size() << "\n";
    std::cout << "render_blocks:   " << geo.renderBlocks.size() << "\n";
    std::cout << "bones:           " << geo.bones.size() << "\n";
    std::cout << "attachments:     " << geo.attachments.size() << "\n";
    std::cout << "collision_verts: " << geo.collisionVertexCount << "\n";
    std::cout << "collision_faces: " << geo.collisionFaceCount << "\n";

    if (!geo.bones.empty())
    {
        std::cout << "\nbones:\n";
        for (size_t i = 0; i < geo.bones.size(); ++i)
        {
            const BoneInfo& b = geo.bones[i];
            std::cout << "  [" << i << "] " << b.name
                      << " parent=" << b.parent << "\n";
        }
    }
    return 0;
}

int cmd_mef(int argc, char** argv)
{
    // argv[0] = "mef", argv[1] = subcommand
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")
    {
        print_mef_help();
        return (argc < 2) ? 1 : 0;
    }

    std::string subcmd = argv[1];

    if (subcmd == "info")
    {
        if (argc < 3)
        {
            std::cerr << "mef info: missing input file\n";
            return 1;
        }
        return do_mef_info(argv[2]);
    }

    if (subcmd == "dump")
    {
        if (argc < 3)
        {
            std::cerr << "mef dump: missing input file\n";
            return 1;
        }

        std::string outpath;
        for (int i = 3; i < argc - 1; ++i)
        {
            if (std::string(argv[i]) == "-o")
            {
                outpath = argv[i + 1];
                break;
            }
        }
        return do_mef_dump(argv[2], outpath);
    }

    if (subcmd == "export")
    {
        if (argc < 3)
        {
            std::cerr << "mef export: missing input\n";
            return 1;
        }

        std::string outpath;
        for (int i = 3; i < argc - 1; ++i)
        {
            if (std::string(argv[i]) == "-o")
            {
                outpath = argv[i + 1];
                break;
            }
        }
        if (outpath.empty())
        {
            std::cerr << "mef export: missing -o <output.obj|output_dir>\n";
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
                std::cerr << "mef export --batch: input is not a directory: " << input << "\n";
                return 2;
            }

            if (!fs::exists(outpath))
            {
                std::error_code ec;
                fs::create_directories(outpath, ec);
                if (ec)
                {
                    std::cerr << "mef: cannot create output dir: " << outpath << " (" << ec.message() << ")\n";
                    return 4;
                }
            }

            bool any_failed = false;
            for (const auto& entry : fs::directory_iterator(input))
            {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".mef") continue;

                std::string stem = entry.path().stem().string();
                std::string obj_out = (fs::path(outpath) / (stem + ".obj")).string();

                int rc = do_mef_export(entry.path().string(), obj_out);
                if (rc != 0)
                {
                    std::cerr << "mef: error processing " << entry.path().filename().string() << " (rc=" << rc << ")\n";
                    any_failed = true;
                }
            }
            return any_failed ? 3 : 0;
        }
        else
        {
            return do_mef_export(input, outpath);
        }
    }

    std::cerr << "mef: unknown subcommand '" << subcmd << "'\n";
    std::cerr << "Run 'gconv mef --help' for usage.\n";
    return 1;
}

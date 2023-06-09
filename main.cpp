#include <iostream>
#include <vector>
#include <cassert>
#include <algorithm>
#include <experimental/filesystem>
#include <fstream>
#include <memory>
#include <hip/hiprtc.h>

namespace fs = std::experimental::filesystem;


fs::path relativePath( const fs::path &path, const fs::path &relative_to )
{
    // create absolute paths
    fs::path p = fs::absolute(path);
    fs::path r = fs::absolute(relative_to);

    // if root paths are different, return absolute path
    if( p.root_path() != r.root_path() )
        return p;

    // initialize relative path
    fs::path result;

    // find out where the two paths diverge
    fs::path::const_iterator itr_path = p.begin();
    fs::path::const_iterator itr_relative_to = r.begin();
    while( itr_path != p.end() && itr_relative_to != r.end() && *itr_path == *itr_relative_to ) {
        ++itr_path;
        ++itr_relative_to;
    }

    // add "../" for each remaining token in relative_to
    if( itr_relative_to != r.end() ) {
        ++itr_relative_to;
        while( itr_relative_to != r.end() ) {
            result /= "..";
            ++itr_relative_to;
        }
    }

    // add remaining path
    while( itr_path != p.end() ) {
        result /= *itr_path;
        ++itr_path;
    }

    return result;
}

template <class T>
T generic_read_file(const std::string& filename, size_t offset = 0, size_t nbytes = 0)
{
    std::ifstream is(filename, std::ios::binary | std::ios::ate);
    if(nbytes == 0)
    {
        // if there is a non-zero offset and nbytes is not set,
        // calculate size of remaining bytes to read
        nbytes = is.tellg();
        if(offset > nbytes) {
            std::cout << "error 1\n";
            throw "offset is larger than file size";
        }
        nbytes -= offset;
    }
    if(nbytes < 1) {
        std::cout << "error2 \n";
        throw "Invalid size for: " + filename;
    }
    is.seekg(offset, std::ios::beg);

    T buffer(nbytes, 0);
    if(not is.read(&buffer[0], nbytes)) {
        std::cout << "erorr 3\n";
        throw "Error reading file: " + filename;
    }
    return buffer;
}

std::string read_buffer(const std::string& filename, size_t offset=0, size_t nbytes=0)
{
    return generic_read_file<std::string>(filename, offset, nbytes);
}

void write_buffer(const std::string& filename, const char* buffer, std::size_t size)
{
    std::ofstream os(filename);
    os.write(buffer, size);
}

void write_buffer(const std::string& filename, const std::vector<char>& buffer)
{
    write_buffer(filename, buffer.data(), buffer.size());
}

struct src_file
{
    fs::path path;
    std::string contents;
    std::size_t len() const { return contents.size(); }
};

using hiprtc_program_ptr = hiprtcProgram;

template <class... Ts>
hiprtc_program_ptr hiprtc_program_create(Ts... xs)
{
    hiprtcProgram prog = nullptr;
    auto result        = hiprtcCreateProgram(&prog, xs...);
    if(result != HIPRTC_SUCCESS) {
        std::cout << "error0\n";
        throw "Create program failed.";
    }
    return prog;
}

struct hiprtc_program
{
    struct string_array
    {
        std::vector<std::string> strings{};
        std::vector<const char*> c_strs{};

        string_array() {}
        string_array(const string_array&) = delete;

        std::size_t size() const { return strings.size(); }

        const char** data() { return c_strs.data(); }

        void push_back(std::string s)
        {
            strings.push_back(std::move(s));
            c_strs.push_back(strings.back().c_str());
        }
    };

    hiprtc_program_ptr prog = nullptr;
    string_array headers{};
    string_array include_names{};
    std::string cpp_src  = "";
    std::string cpp_name = "";
    std::string output_buffer = "";
    hiprtc_program(const std::vector<src_file>& srcs)
    {
        for(auto&& src : srcs)
        {
            std::string content = src.contents;
            std::string path = src.path.string();
			std::cout <<"path in src: " << path << "\n";
            if(src.path.extension().string() == ".cpp")
            {
                output_buffer = src.path.stem().string()+".o";
                cpp_src  = std::move(content);
                cpp_name = std::move(path);
            }
            else
            {
                headers.push_back(std::move(content));
                include_names.push_back(std::move(path));
            }
        }
		std::cout << "done creating buffers\n";
		prog = hiprtc_program_create(cpp_src.c_str(),
                                     cpp_name.c_str(),
                                     headers.size(),
                                     headers.data(),
                                     include_names.data());
		std::cout <<"program creation done\n";

    }

    void compile(const std::vector<std::string>& options)
    {
        std::vector<const char*> c_options;
        std::transform(options.begin(),
                       options.end(),
                       std::back_inserter(c_options),
                       [](const std::string& s) { return s.c_str(); });
        auto result   = hiprtcCompileProgram(prog, c_options.size(), c_options.data());
		std::cout << "compilation done\n";
        auto compile_log = log();
        if(!compile_log.empty()) {
            std::cout << compile_log << std::endl;
        }
        dump_code_obj(); 
        if(result != HIPRTC_SUCCESS) {
            std::cout << "compilation failed:\n";
            throw "Compilation failed.";
        }
    }

    std::string log() const
    {
        std::size_t n = 0;
        hiprtcGetProgramLogSize(prog, &n);
        if(n == 0)
            return {};
        std::string buffer(n, '\0');
        hiprtcGetProgramLog(prog, &buffer[0]);
        assert(buffer.back() != 0);
        return buffer;
    }

    void dump_code_obj() const
    {
        std::size_t n = 0;
        hiprtcGetCodeSize(prog, &n);
        std::vector<char> buffer(n);
        hiprtcGetCode(prog, buffer.data());
        write_buffer(output_buffer, buffer);
    }
};

int main(int argc, char **argv) 
{
    fs::path current_path = fs::current_path();
    std::vector<std::string> compile_options;
    for(int i = 1; i < argc; i++) {
        compile_options.push_back(std::string(argv[i]));
    }
    std::vector<src_file> srcs;
    for(const fs::directory_entry& dir_file : fs::recursive_directory_iterator(current_path)) {
        fs::path file_path  = dir_file.path();
        if(fs::is_directory(file_path)) {
            continue;
        }
        std::string filename = file_path.filename().string();
		fs::path relative_path = relativePath(file_path, current_path);
		std::cout << "relative_path: " << relative_path << std::endl;
        std::string contents = read_buffer(file_path.string());
        src_file tmp{relative_path, contents};
        srcs.push_back(tmp);
    }
	std::cout << "done pushing srcs\n";
    hiprtc_program prog{srcs};
    prog.compile(compile_options);
    return 0;
}

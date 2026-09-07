#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <getopt.h>
#include "pugixml.hpp"

namespace fs = std::filesystem;

struct Config {
    std::string inputFile;
    bool batchMode = false;
    std::string directory;
    bool recursive = false;
    int indent = 4;
    bool removeComments = false;
    bool shortTags = true;
    bool removeBlankTexts = false;
    bool removeEmptyLines = false;
    std::string encoding = "UTF-8";
    std::vector<std::string> extensions = {".xml"};
    bool noXmlDeclaration = false;
    bool standalone = false;
};

void print_version() {
    std::cout << "xmlfile CLI v1.0.0" << std::endl;
}

void print_help() {
    std::cout << "Uso: xmlfile [opções]\n"
              << "Opções:\n"
              << "  -f, --file <path>           Arquivo XML para processar\n"
              << "  -b, --batch                 Habilitar processamento em lote (usar com -d)\n"
              << "  -d, --dir <path>            Diretório para processar\n"
              << "  -r, --recursive             Processar subdiretórios recursivamente\n"
              << "  -i, --indent <num>          Número de espaços para indentação (padrão: 4)\n"
              << "  -c, --remove-comments       Remover comentários XML\n"
              << "  -s, --short-tags            Usar tags curtas para elementos vazios (padrão: true)\n"
              << "  -t, --remove-blank-texts    Remover textos em branco entre tags\n"
              << "  -l, --remove-empty-lines    Remover linhas vazias (implícito na formatação)\n"
              << "  -e, --encoding <enc>        Codificação de saída (padrão: UTF-8)\n"
              << "  -u, --include-extensions    Extensões extras separadas por vírgula (ex: i3d,gltf)\n"
              << "  -x, --non-xml-declaration   Não incluir declaração XML\n"
              << "  -a, --standalone            Adicionar standalone=\"yes\" na declaração\n"
              << "  -v, --version               Mostrar versão\n";
}

pugi::xml_encoding get_pugi_encoding(const std::string& enc) {
    std::string upper_enc = enc;
    std::transform(upper_enc.begin(), upper_enc.end(), upper_enc.begin(), ::toupper);
    if (upper_enc == "UTF-8") return pugi::encoding_utf8;
    if (upper_enc == "UTF-16") return pugi::encoding_utf16;
    if (upper_enc == "UTF-32") return pugi::encoding_utf32;
    if (upper_enc == "LATIN1") return pugi::encoding_latin1;
    return pugi::encoding_utf8;
}

void format_xml(const fs::path& path, const Config& config) {
    if (fs::is_directory(path)) {
        std::cerr << "Erro: '" << path << "' é um diretório; use -d para processar diretórios" << std::endl;
        return;
    }

    pugi::xml_document doc;
    unsigned int load_flags = pugi::parse_default;

    if (config.removeBlankTexts) {
        load_flags |= pugi::parse_ws_pcdata;
    }

    pugi::xml_parse_result result = doc.load_file(path.c_str(), load_flags);
    if (!result) {
        std::cerr << "Erro ao carregar " << path << ": " << result.description() << std::endl;
        return;
    }

    if (config.removeBlankTexts) {
        struct blank_remover : pugi::xml_tree_walker {
            std::vector<std::pair<pugi::xml_node, bool>> nodes;
            virtual bool for_each(pugi::xml_node& node) override {
                if (node.type() == pugi::node_pcdata) {
                    std::string text = node.value();
                    if (text.find_first_not_of(" \t\n\r") == std::string::npos) {
                        bool between_elements = node.previous_sibling().type() == pugi::node_element
                                             && node.next_sibling().type() == pugi::node_element;
                        nodes.emplace_back(node, between_elements);
                    }
                }
                return true;
            }
        } remover;
        doc.traverse(remover);
        for (auto& entry : remover.nodes) {
            pugi::xml_node n = entry.first;
            if (entry.second) {
                n.parent().insert_child_after(pugi::node_pcdata, n).set_value(" ");
            }
            n.parent().remove_child(n);
        }
    }

    if (config.removeComments) {
        struct node_retriever : pugi::xml_tree_walker {
            std::vector<pugi::xml_node> nodes;
            virtual bool for_each(pugi::xml_node& node) {
                if (node.type() == pugi::node_comment) nodes.push_back(node);
                return true;
            }
        } retriever;
        doc.traverse(retriever);
        for (auto& n : retriever.nodes) n.parent().remove_child(n);
    }

    if (config.standalone && config.noXmlDeclaration) {
        std::cerr << "Aviso: -a (standalone) ignorado porque -x remove a declaração XML" << std::endl;
    }

    if (config.standalone && !config.noXmlDeclaration) {
        pugi::xml_node decl = doc.child("xml");
        std::string lower_enc = config.encoding;
        std::transform(lower_enc.begin(), lower_enc.end(), lower_enc.begin(), ::tolower);
        if (!decl || decl.type() != pugi::node_declaration) {
            decl = doc.prepend_child(pugi::node_declaration);
            decl.append_attribute("version") = "1.0";
            decl.append_attribute("encoding") = lower_enc.c_str();
        }
        if (!decl.attribute("standalone")) {
            decl.append_attribute("standalone") = "yes";
        } else {
            decl.attribute("standalone") = "yes";
        }
    }

    unsigned int save_flags = pugi::format_indent;
    if (config.noXmlDeclaration) save_flags |= pugi::format_no_declaration;
    if (!config.shortTags) save_flags |= pugi::format_no_empty_element_tags;

    std::string indent_str(config.indent, ' ');
    pugi::xml_encoding enc = get_pugi_encoding(config.encoding);

    if (doc.save_file(path.c_str(), indent_str.c_str(), save_flags, enc)) {
        if (config.removeEmptyLines) {
            std::ifstream in(path);
            std::string content((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
            std::string out;
            out.reserve(content.size());
            bool prev_newline = false;
            for (size_t i = 0; i < content.size(); ++i) {
                char c = content[i];
                if (c == '\n') {
                    if (!prev_newline) out.push_back(c);
                    prev_newline = true;
                } else {
                    out.push_back(c);
                    prev_newline = false;
                }
            }
            std::ofstream of(path);
            of << out;
        }
        std::cout << "Formatado com sucesso: " << path << std::endl;
    } else {
        std::cerr << "Erro ao salvar: " << path << std::endl;
    }
}

void process_directory(const fs::path& dir, const Config& config) {
    if (!fs::exists(dir)) {
        std::cerr << "Diretório não encontrado: " << dir << std::endl;
        return;
    }

    auto process_entry = [&](const fs::directory_entry& entry) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            for (const auto& target_ext : config.extensions) {
                if (ext == target_ext) {
                    format_xml(entry.path(), config);
                    break;
                }
            }
        }
    };

    if (config.recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::none)) {
            process_entry(entry);
        }
    } else {
        for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::none)) {
            process_entry(entry);
        }
    }
}

int main(int argc, char* argv[]) {
    Config config;
    static struct option long_options[] = {
        {"file", required_argument, 0, 'f'},
        {"batch", no_argument, 0, 'b'},
        {"dir", required_argument, 0, 'd'},
        {"recursive", no_argument, 0, 'r'},
        {"indent", required_argument, 0, 'i'},
        {"remove-comments", no_argument, 0, 'c'},
        {"short-tags", no_argument, 0, 's'},
        {"remove-blank-texts", no_argument, 0, 't'},
        {"remove-empty-lines", no_argument, 0, 'l'},
        {"encoding", required_argument, 0, 'e'},
        {"include-extensions", required_argument, 0, 'u'},
        {"non-xml-declaration", no_argument, 0, 'x'},
        {"standalone", no_argument, 0, 'a'},
        {"version", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "f:bd:ri:cstle:u:xavh", long_options, nullptr)) != -1) {
        auto require_arg = [&](const char* opt_name) -> std::string {
            if (!optarg || optarg[0] == '\0') {
                std::cerr << "Erro: " << opt_name << " requer um argumento" << std::endl;
                std::exit(1);
            }
            if (optarg[0] == '-') {
                std::cerr << "Erro: " << opt_name << " requer um argumento (recebido: '" << optarg << "')" << std::endl;
                std::exit(1);
            }
            return std::string(optarg);
        };
        switch (opt) {
            case 'f': config.inputFile = require_arg("-f"); break;
            case 'b': config.batchMode = true; break;
            case 'd': config.directory = require_arg("-d"); break;
            case 'r': config.recursive = true; break;
            case 'i': {
                const std::string s = require_arg("-i");
                int value = 0;
                try {
                    size_t pos = 0;
                    value = std::stoi(s, &pos);
                    if (pos != s.size()) throw std::invalid_argument("trailing chars");
                } catch (const std::exception&) {
                    std::cerr << "Erro: -i requer um número inteiro (recebido: '" << s << "')" << std::endl;
                    return 1;
                }
                if (value < 0 || value > 16) {
                    std::cerr << "Erro: -i deve estar entre 0 e 16 (recebido: " << value << ")" << std::endl;
                    return 1;
                }
                config.indent = value;
                break;
            }
            case 'c': config.removeComments = true; break;
            case 's': config.shortTags = !config.shortTags; break;
            case 't': config.removeBlankTexts = true; break;
            case 'l': config.removeEmptyLines = true; break;
            case 'e': config.encoding = require_arg("-e"); break;
            case 'u': {
                std::string exts = require_arg("-u");
                size_t pos = 0;
                auto add_ext = [&](std::string e) {
                    if (e.empty()) return;
                    if (e[0] != '.') e = "." + e;
                    config.extensions.push_back(e);
                };
                while ((pos = exts.find(',')) != std::string::npos) {
                    add_ext(exts.substr(0, pos));
                    exts.erase(0, pos + 1);
                }
                add_ext(exts);
                break;
            }
            case 'x': config.noXmlDeclaration = true; break;
            case 'a': config.standalone = true; break;
            case 'v': print_version(); return 0;
            case 'h': print_help(); return 0;
            default: return 1;
        }
    }

    if (!config.inputFile.empty()) {
        format_xml(config.inputFile, config);
    } else if (!config.directory.empty()) {
        process_directory(config.directory, config);
    } else {
        print_help();
    }

    return 0;
}

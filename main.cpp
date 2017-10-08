/**
    @file
    @copyright
        Copyright (C) 2017 Michael Adam
        Copyright (C) 2017 Bernd Amend
        Copyright (C) 2017 Stefan Rommel

        This program is free software: you can redistribute it and/or modify
        it under the terms of the GNU Lesser General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        any later version.

        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU Lesser General Public License
        along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <iostream>

#include <string>
#include <vector>

#include <boost/program_options.hpp>
#include <docmala/Docmala.h>

using namespace std;

int main(int argc, char* argv[]) {
    boost::program_options::options_description desc("Documentation Markup Language");
    desc.add_options()("help", "produce this help message")("input,i", boost::program_options::value<std::string>(), "input file")(
        "outputdir,o",
        boost::program_options::value<std::string>(),
        "output directory")("outputplugins,p", boost::program_options::value<std::vector<std::string>>(), "plugins for output generation")(
        "parameters",
        boost::program_options::value<std::vector<std::string>>()->multitoken(),
        "parameters for plugins in form [key]=[value] or [key] for flags")("listoutputplugins,l", "print a list of output plugins");

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }

    docmala::Docmala       docmala;
    docmala::ParameterList parameters;

    if (vm.count("listoutputplugins")) {
        for (auto plugin : docmala.listOutputPlugins())
            std::cout << plugin << "\n";
        return 0;
    }

    std::string outputDir;
    std::string inputFile;

    if (vm.count("input")) {
        inputFile = vm["input"].as<std::string>();
    } else {
        std::cout << "An input file has to be specified\n";
        return 1;
    }

    if (vm.count("outputdir")) {
        outputDir = vm["outputdir"].as<std::string>();
    } else {
        outputDir = inputFile.substr(0, inputFile.find_last_of("\\/"));
    }

    if (vm.count("parameters")) {
        for (auto parameter : vm["parameters"].as<std::vector<std::string>>()) {
            auto equalsLocation = parameter.find_first_of('=');
            if (equalsLocation != std::string::npos) {
                std::string key   = parameter.substr(0, equalsLocation);
                std::string value = parameter.substr(equalsLocation + 1);
                parameters.insert(std::make_pair(key, docmala::Parameter{key, value, docmala::FileLocation()}));
            } else {
                parameters.insert(std::make_pair(parameter, docmala::Parameter{parameter, "", docmala::FileLocation()}));
            }
        }
    }

    parameters.insert(std::make_pair("outputdir", docmala::Parameter{"outputdir", outputDir, docmala::FileLocation()}));
    docmala.setParameters(parameters);
    docmala.parseFile(inputFile);

    for (const auto& error : docmala.errors()) {
        std::cout << error.location.fileName << "(" << error.location.line << ":" << error.location.column << "): " << error.message
                  << std::endl;
    }

    if (vm.count("outputplugins")) {
        for (auto plugin : vm["outputplugins"].as<std::vector<std::string>>()) {
            if (!docmala.produceOutput(plugin)) {
                std::cout << "Unable to create output for plugin: " << plugin << "\n";
                return -1;
            }
        }
    } else {
        std::cout << "No output plugin specified. No output is generated.\n";
        return -1;
    }

    return 0;
}

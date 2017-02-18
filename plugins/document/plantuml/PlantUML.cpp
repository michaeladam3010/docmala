#include <extension_system/Extension.hpp>
#include <docmala/DocmaPlugin.h>
#include <fstream>
#include <spawn.h>
#include <sys/wait.h>
#include <sstream>
#include <unordered_map>

using namespace docmala;

class PlantUMLPlugin : public DocumentPlugin {
    // DocmaPlugin interface
public:
    BlockProcessing blockProcessing() const override;
    bool process( const ParameterList &parameters, const FileLocation &location, Document &document, const std::string &block) override;

    std::unordered_map<std::string, DocumentPart::Image> _cache;
};


DocumentPlugin::BlockProcessing PlantUMLPlugin::blockProcessing() const {
    return BlockProcessing::Required;
}

bool PlantUMLPlugin::process(const ParameterList &parameters, const FileLocation &location, Document &document, const std::string &block)
{
    std::stringstream outputFileName;

    auto cachePosition = _cache.find(block);
    std::string pluginDir;

    if( cachePosition != _cache.end() ) {
        DocumentPart::Image image = cachePosition->second;
        image.line = location.line;
        document.addPart( image );
        return true;
    }

    auto outPathIter = parameters.find("outputDir");
    if( outPathIter != parameters.end() ) {
        outputFileName << outPathIter->second.value << "/";
    }

    auto pluginDirIter = parameters.find("pluginDir");
    if( pluginDirIter != parameters.end() ) {
        pluginDir =  pluginDirIter->second.value + '/';
    }

    outputFileName << "uml_" << "_" << location.line <<".png";

    std::string outFile = "@startuml\n";
    outFile += block + "\n";
    outFile += "@enduml \n";

    int exit_code = 0;
    int in[2] = {0};
    int out[2] = {0};
    std::string stringargs [] = {"java", "-jar", pluginDir + "plantuml.jar", "-p"};
    char *args [] = {&stringargs[0][0], &stringargs[1][0], &stringargs[2][0], &stringargs[3][0], nullptr};

    posix_spawn_file_actions_t action;

    if( pipe(in) || pipe(out) ) {
        // ERROR
        return false;
    }

    posix_spawn_file_actions_init(&action);

    posix_spawn_file_actions_adddup2(&action, out[0], 0);
    posix_spawn_file_actions_addclose(&action, out[1]);

    posix_spawn_file_actions_adddup2(&action, in[1], 1);
    posix_spawn_file_actions_addclose(&action, in[0]);

    pid_t pid = 0;

    if(posix_spawnp(&pid, args[0] , &action, NULL, &args[0], NULL) != 0) {
        //cout << "posix_spawnp failed with error: " << strerror(errno) << "\n";
        return false;
    }

    close(out[0]);
    close(in[1]);

    write(out[1], outFile.data(), outFile.length() );
    close(out[1]);

    std::string imageData;

    while( true )
    {
        char buffer[1024];
        ssize_t readData = read(in[0], buffer, 1024);
        if( readData > 0 )
        {
            imageData.append(buffer, static_cast<size_t>(readData));
        } else {
            break;
        }

    }

    waitpid(pid,&exit_code,0);
    posix_spawn_file_actions_destroy(&action);

    DocumentPart::Text text(location.line);
    DocumentPart::Image image("png", imageData, text);
    _cache.insert(std::make_pair(block, image));
    document.addPart( image );
    return true;
}

EXTENSION_SYSTEM_EXTENSION(docmala::DocumentPlugin, PlantUMLPlugin, "plantuml", 1, "Creates an uml diagram form the subsequent block", EXTENSION_SYSTEM_NO_USER_DATA )

#include <extension_system/Extension.hpp>
#include <docmala/DocmaPlugin.h>
#include <fstream>
#include <sstream>

#include "HtmlOutput.h"

static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

std::string base64_encode(const std::string& inputData) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for( auto data = inputData.begin(); data != inputData.end(); data++)
    {
        char_array_3[i++] = *data;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i <4) ; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i)
    {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = ( char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] =   char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while((i++ < 3))
            ret += '=';

    }

    return ret;
}


using namespace docmala;

class HtmlOutputPlugin : public OutputPlugin
{
    // OutputPlugin interface
public:
    bool write(const ParameterList &parameters, const Document &document) override {
        std::string nameBase = "outfile";
        std::string outputFileName = "outfile.html";

        auto inputFile = parameters.find("inputFile");

        if( inputFile != parameters.end() ) {
            outputFileName = inputFile->second.value;
            nameBase = outputFileName.substr(0, outputFileName.find_last_of("."));
            outputFileName = nameBase + ".html";
        }

        std::ofstream outFile;

        outFile.open(outputFileName);

        if( outFile.is_open() ) {
            HtmlOutput output;
            outFile << output.produceHtml(parameters, document);
            return true;
        }
        return false;
    }
};


std::string id(const DocumentPart::VisualElement *element)
{
    if( element->line > 0 )
        return std::string(" id=\"line_") + std::to_string(element->line) +"\"";
    return "";
}

void writeText(std::stringstream &outFile, const DocumentPart::Text *printText, bool isGenerated)
{
    if( !isGenerated ) {
        outFile << "<span " << id(printText) << ">";
    }

    for( const auto &text : printText->text ) {
        if( text.bold ) {
            outFile << "<b>";
        }
        if( text.italic ) {
            outFile << "<i>";
        }
        if( text.crossedOut ) {
            outFile << "<del>";
        }
        outFile << text.text;

        if( text.bold ) {
            outFile << "</b>";
        }
        if( text.italic ) {
            outFile << "</i>";
        }
        if( text.crossedOut ) {
            outFile << "</del>";
        }
    }

    if( !isGenerated ) {
        outFile << "</span>" << std::endl;
    }
}

void writeList(std::stringstream &outFile, std::vector<DocumentPart>::const_iterator &start, const Document &document, bool isGenerated, int currentLevel = 0)
{
    for( ; start != document.parts().end(); start++ ) {
        if( start->type() != DocumentPart::Type::List ) {
            start--;
            return;
        }

        // TODO: Currently, mixed lists are not upported, meaning that:
        // * text
        // # text 2
        // will be treated as:
        // * text
        // * text 2

        auto list = start->list();

        if( currentLevel == list->level ) {
            for( auto entry : list->entries ) {
                outFile << "<li> ";
                writeText(outFile, &entry, isGenerated);
                outFile << " </li>" << std::endl;
            }
            return;
        } else if( currentLevel < list->level ) {
            std::string type = "ul";
            std::string style;
            switch( list->type ) {
            case DocumentPart::List::Type::Points:
                type = "ul";
                break;
            case DocumentPart::List::Type::Dashes:
                type = "ul";
                style = "class=\"dash\"";
                break;
            case DocumentPart::List::Type::Numbered:
                type = "ol";
                break;
            }

            currentLevel++;
            outFile << "<" << type << " " << style << ">" << std::endl;
            writeList(outFile, start, document, isGenerated, currentLevel);
            outFile << "</" << type << ">" << std::endl;
            currentLevel--;
        } else {
            return;
        }
    }
    start--;
}

void HtmlOutput::writeDocumentParts(std::stringstream &outFile, const ParameterList &parameters, const Document &document, const std::vector<DocumentPart> &documentParts, bool isGenerated)
{
    bool paragraphOpen = false;
    auto previous = document.parts().end();


    bool embedImages = parameters.find("embedImages") != parameters.end();


    for( std::vector<DocumentPart>::const_iterator part = documentParts.begin(); part != documentParts.end(); part++ ) {
        switch(part->type() ) {
        case DocumentPart::Type::Invalid:
            break;
        case DocumentPart::Type::Custom:
            break;
        case DocumentPart::Type::Headline: {
            if( paragraphOpen ) {
                outFile << "</p>" << std::endl;
                paragraphOpen = false;
            }
            auto headline = part->headline();
            outFile << "<h" << headline->level << ">";
            writeText(outFile, headline, isGenerated);
            outFile << "</h" << headline->level << ">" << std::endl;
            break;
        }
        case DocumentPart::Type::Text: {
            auto text = part->text();
            writeText(outFile, text, isGenerated);
            break;
        }
        case DocumentPart::Type::Paragraph:
            if( paragraphOpen ) {
                outFile << "</p>" << std::endl;
                paragraphOpen = false;
            }
            outFile << "<p>" << std::endl;
            paragraphOpen = true;
            break;
        case DocumentPart::Type::Image: {
            auto image = part->image();
            outFile << "<figure"<<id(image)<<">" << std::endl;
            if( embedImages ) {
                outFile << "<img src=\"data:image/" << image->format << ";base64,";
                outFile << base64_encode( image->data );
                outFile << "\">";
            } else {
                std::ofstream imgFile;
                std::stringstream fileName;
                fileName << _nameBase << "_image_" << _imageCounter << "." << image->format;

                imgFile.open(fileName.str());
                imgFile << image->data;
                imgFile.close();
                outFile << "<img src=\"" << fileName.str() <<"\">" << std::endl;
            }
            if( previous != document.parts().end() && previous->type() == DocumentPart::Type::Caption ) {
                outFile << "<figcaption>Figure " << _figureCounter << ": ";
                writeText(outFile, previous->caption(), isGenerated );
                outFile << "</figcaption>" << std::endl;
                _figureCounter++;
            }
            outFile << "</figure>" << std::endl;
            _imageCounter++;
            break;
        }
        case DocumentPart::Type::List: {
            writeList(outFile, part, document, isGenerated);
            break;
        }
        case DocumentPart::Type::GeneratedDocument: {
            auto generated = part->generatedDocument();
            outFile << "<div " << id(generated) << ">" << std::endl;
            writeDocumentParts(outFile, parameters, document, generated->document, true);
            outFile << "</div>" << std::endl;
            break;
        }
        default:
            break;
        }
        previous = part;
    }

    if( paragraphOpen ) {
        outFile << "</p>" << std::endl;
        paragraphOpen = false;
    }

}

std::string HtmlOutput::produceHtml(const ParameterList &parameters, const Document &document, const std::string &scripts)
{
    std::string outputFileName = "outfile.html";

    auto inputFile = parameters.find("inputFile");

    if( inputFile != parameters.end() ) {
        outputFileName = inputFile->second.value;
        _nameBase = outputFileName.substr(0, outputFileName.find_last_of("."));
        outputFileName = _nameBase + ".html";
    }

    std::stringstream outFile;


    outFile << "<!doctype html>" << std::endl;
    outFile << "<html>" << std::endl;
    outFile << "<head>" << std::endl;
    outFile << "<meta charset=\"utf-8\">" << std::endl;
    outFile << "<title>No title yet</title>" << std::endl;

    outFile << "<style>" << std::endl;
    outFile << "ul.dash { list-style-type: none; }" << std::endl;

    outFile << "ul.dash li:before { content: '-'; position: absolute; margin-left: -15px; }" << std::endl;
    outFile << "</style>" << std::endl;

    outFile << "<script>" << std::endl;
    outFile << scripts << std::endl;
    outFile << "</script>" << std::endl;
    outFile << "</head>" << std::endl;
    outFile << "<body>" << std::endl;
    outFile << "" << std::endl;
    outFile << "" << std::endl;

    outFile << "</body>" << std::endl;

    writeDocumentParts(outFile, parameters, document, document.parts() );

    return outFile.str();
}

EXTENSION_SYSTEM_EXTENSION(docmala::OutputPlugin, HtmlOutputPlugin, "html", 1, "Write document to a HTML file", EXTENSION_SYSTEM_NO_USER_DATA )

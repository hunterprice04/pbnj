#include "Configuration.h"
#include "ConfigReader.h"
#include "TransferFunction.h"

#include "rapidjson/document.h"

#include <glob.h>
#include <iostream>

namespace pbnj {

Configuration::Configuration(std::string filename) :
    configFilename(filename)
{
    //get a parsed json document from the file
    this->reader = new ConfigReader();
    rapidjson::Document json;
    this->reader->parseConfigFile(filename, json);

    /* the following are required:
     *  - data filename
     *  - data dimensions <- should be required only for raw
     *  - image size/dimensions (ie width and height)
     *  - image filename
     * everything else is optional
     */

    if(!json.HasMember("filename"))
        std::cerr << "Data filename is required!" << std::endl;
    else {
        std::string fname = json["filename"].GetString();

        // check to see if the filename provided is actually a glob
        glob_t globResult;
        // GLOB_ERR - immediately return on error, don't continue
        // GLOB_BRACE - allow brace expansion
        //              e.g. /path/{one,two} => /path/one /path/two
        // GLOB_NOMAGIC - return the word if there are no metacharacters
        //                in the string, even if there is no match. This
        //                is preferred over GLOB_NOCHECK because it limits
        //                gl_pathv to containing only path(s) without
        //                metacharacters
        // GLOB_TILDE_CHECK - replace ~ or ~username with home directories
        //                    and fail if not found
        unsigned int globError = glob(fname.c_str(),
                GLOB_ERR | GLOB_BRACE | GLOB_TILDE_CHECK | GLOB_NOMAGIC, NULL,
                &globResult);

        if(globError == GLOB_NOSPACE) {
            std::cerr << "ERROR: Ran out of memory when globbing files with";
            std::cerr << "pattern " << fname << std::endl;
        }
        else if(globError == GLOB_ABORTED) {
            std::cerr << "ERROR: Read error when globbing files with";
            std::cerr << "pattern " << fname << std::endl;
        }
        else if(globError == GLOB_NOMATCH) {
            std::cerr << "ERROR: No matches found when globbing files with";
            std::cerr << "pattern " << fname << std::endl;
        }
        else {
            // either there was a glob that returned a single path, or there
            // were no metacharacters to glob with, in which case the path may
            // be valid or invalid
            if(globResult.gl_pathc == 1) {
                this->dataFilename = fname;
            }
            // there was a list of paths successfully globbed
            else {
                for(int i = 0; i < globResult.gl_pathc; i++)
                    this->globbedFilenames.push_back(globResult.gl_pathv[i]);
            }
        }
    }

    if(!json.HasMember("dimensions"))
        std::cerr << "Data dimensions are required!" << std::endl;
    else {
        const rapidjson::Value& dataDim = json["dimensions"];
        this->dataXDim = dataDim[0].GetInt();
        this->dataYDim = dataDim[1].GetInt();
        this->dataZDim = dataDim[2].GetInt();
    }

    if(!json.HasMember("imageSize"))
        std::cerr << "Image dimensions are required!" << std::endl;
    else {
        const rapidjson::Value& imageDim = json["imageSize"];
        this->imageWidth = imageDim[0].GetInt();
        this->imageHeight = imageDim[1].GetInt();
    }

    if(!json.HasMember("outputImageFilename"))
        std::cerr << "Image filename is required!" << std::endl;
    else
        this->imageFilename = json["outputImageFilename"].GetString();

    // choice of variable for netcdf files
    if(json.HasMember("dataVariable"))
        this->dataVariable = json["dataVariable"].GetString();

    // if no color map is requested, the transfer function will just
    // use a black to white default
    if(json.HasMember("colorMap"))
        this->selectColorMap(json["colorMap"].GetString());

    // opacity map is a ramp by default, otherwise get a list from the user
    if(json.HasMember("opacityMap")) {
        const rapidjson::Value& omap = json["opacityMap"];
        for(rapidjson::SizeType i = 0; i < omap.Size(); i++)
            this->opacityMap.push_back(omap[i].GetFloat());
    }

    // opacity attenuation >= 1.0 doesn't do anything
    if(json.HasMember("opacityAttenuation"))
        this->opacityAttenuation = json["opacityAttenuation"].GetFloat();
    else
        this->opacityAttenuation = 1.0;

    // samples per pixel
    if(json.HasMember("samplesPerPixel")) {
        unsigned int val = json["samplesPerPixel"].GetUint();
        std::cerr << "samples: " << val << std::endl;
        this->samples = val;
    }
    else
        this->samples = 4;

    // allow a camera position, else use the camera's default of 0,0,0
    if(json.HasMember("cameraPosition")) {
        const rapidjson::Value& cameraPos = json["cameraPosition"];
        this->cameraX = cameraPos[0].GetFloat();
        this->cameraY = cameraPos[1].GetFloat();
        this->cameraZ = cameraPos[2].GetFloat();
    }
    else {
        this->cameraX = 0.0;
        this->cameraY = 0.0;
        this->cameraZ = 0.0;
    }

    // allow a camera up vector, else use the camera's default of 0, 1, 0
    if(json.HasMember("cameraUpVector")) {
        const rapidjson::Value& cameraUpVector = json["cameraUpVector"];
        this->cameraUpX = cameraUpVector[0].GetFloat();
        this->cameraUpY = cameraUpVector[1].GetFloat();
        this->cameraUpZ = cameraUpVector[2].GetFloat();
    }
    else {
        this->cameraUpX = 0.0;
        this->cameraUpY = 1.0;
        this->cameraUpZ = 0.0;
    }
}

void Configuration::selectColorMap(std::string userInput)
{
    // some simple alternatives to color map names are allowed
    if(userInput.compare("coolToWarm") == 0 ||
       userInput.compare("cool to warm") == 0) {
        this->colorMap = coolToWarm;
    }
    else if(userInput.compare("spectralReverse") == 0 ||
            userInput.compare("spectral reverse") == 0 ||
            userInput.compare("reverse spectral") == 0) {
        this->colorMap = spectralReverse;
    }
    else if(userInput.compare("magma") == 0) {
        this->colorMap = magma;
    }
    else if(userInput.compare("viridis") == 0) {
        this->colorMap = viridis;
    }
    else {
        // will default to black to white
        std::cerr << "Unrecognized color map " << userInput << "!" << std::endl;
    }
}

}

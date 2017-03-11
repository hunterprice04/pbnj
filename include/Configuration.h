#ifndef PBNJ_CONFIGURATION_H
#define PBNJ_CONFIGURATION_H

#include <ConfigReader.h>

#include <string>
#include <vector>

namespace pbnj {

    class Configuration {

        public:
            Configuration(std::string filename);

            std::string configFilename;

            std::string dataFilename;
            std::string dataVariable;
            int dataXDim;
            int dataYDim;
            int dataZDim;

            int imageWidth;
            int imageHeight;
            std::string imageFilename;

            std::vector<float> colorMap;
            std::vector<float> opacityMap;
            float opacityAttenuation;

            unsigned int samples;

            float cameraX;
            float cameraY;
            float cameraZ;

            float cameraUpX;
            float cameraUpY;
            float cameraUpZ;

        private:
            ConfigReader *reader;

            void selectColorMap(std::string userInput);
    };

}

#endif

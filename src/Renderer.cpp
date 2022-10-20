#include "Camera.h"
#include "Renderer.h"
#include "Volume.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <vector>

#include <stdlib.h>
#include <stdio.h>

#include <ospray/ospray.h>
#include "ospray/ospray_cpp.h"
#include <ospray/ospray_util.h>
// #include "ospray/ospray_cpp/ext/rkcommon.h"

#include "lodepng/lodepng.h"
#include "CImg.h"

namespace pbnj {

Renderer::Renderer() :
    backgroundColor(), samples(1)
{
    this->oRenderer = ospNewRenderer("scivis");

    this->setBackgroundColor(0, 0, 0, 0);
    this->oCamera = NULL;
    this->oModel = NULL;
    this->oWorld = NULL;
    this->oSurface = NULL;
    this->oMaterial = NULL;
    this->oInstance = NULL;
    // this->oWorld = NULL;
    this->lastVolumeID = "unset";
    this->lastCameraID = "unset";
}

Renderer::~Renderer()
{
    ospRemoveParam(this->oRenderer, "bgColor");
    ospRemoveParam(this->oRenderer, "spp");
    ospRemoveParam(this->oRenderer, "lights");
    ospRemoveParam(this->oRenderer, "aoSamples");
    ospRemoveParam(this->oRenderer, "shadowsEnabled");
    ospRemoveParam(this->oRenderer, "oneSidedLighting");
    ospRemoveParam(this->oRenderer, "model");
    ospRemoveParam(this->oRenderer, "camera");
    ospRemoveParam(this->oRenderer, "world");
    ospRelease(this->oRenderer);

    ospRelease(this->oCamera);

    ospRelease(this->oModel);
    
    ospRemoveParam(this->oMaterial, "Kd");
    ospRemoveParam(this->oMaterial, "Ks");
    ospRemoveParam(this->oMaterial, "Ns");
    ospRelease(this->oMaterial);

    ospRemoveParam(this->oSurface, "isovalues");
    ospRemoveParam(this->oSurface, "volume");
    ospRelease(this->oSurface);

    for(int light = 0; light < this->lights.size(); light++) {
        ospRemoveParam(this->lights[light], "angularDiameter");
        ospRemoveParam(this->lights[light], "direction");
        ospRelease(this->lights[light]);
    }

    ospRelease(this->oGroup);


    ospRemoveParam(this->oInstance, "group");
    ospRelease(this->oInstance);


    ospRemoveParam(this->oWorld, "instance");
    ospRemoveParam(this->oWorld, "light");
    ospRelease(this->oWorld);

}

void Renderer::setBackgroundColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    this->backgroundColor[0] = r;
    this->backgroundColor[1] = g;
    this->backgroundColor[2] = b;
    this->backgroundColor[3] = a;
    float asVec[] = {r/(float)255.0, g/(float)255.0, b/(float)255.0, a/(float)255.0};
    ospSetVec4f(this->oRenderer, "bgColor", r/(float)255.0, g/(float)255.0, b/(float)255.0, a/(float)255.0);
    ospCommit(this->oRenderer);
}

void Renderer::setBackgroundColor(std::vector<unsigned char> bgColor)
{
    // if the incoming vector is empty, it's probably from the config
    // just set to black instead
    if(bgColor.empty() || bgColor.size() < 3)
        this->setBackgroundColor(0, 0, 0, 0);
    else if (bgColor.size() == 3)
        this->setBackgroundColor(bgColor[0], bgColor[1], bgColor[2], 255);
    else
        this->setBackgroundColor(bgColor[0], bgColor[1], bgColor[2], bgColor[3]);
}

void Renderer::setVolume(Volume *v)
{
    if(this->lastVolumeID == v->ID && this->lastRenderType == "volume") {
        // this is the same volume as the current model and we previously
        // did a volume render
        return;
    }
    if(this->oWorld != NULL) {
        ospRelease(this->oModel);
        ospRelease(this->oGroup);
        ospRelease(this->oInstance);
        ospRelease(this->oWorld);

        this->oModel = NULL;
        this->oGroup = NULL;
        this->oInstance = NULL;
        this->oWorld = NULL;
    }

    this->lastVolumeID = v->ID;
    this->lastRenderType = "volume";

    // put the volume in a model and commit it
    this->oModel = ospNewVolumetricModel(v->asOSPRayObject());
    ospCommit(this->oModel);

    // put the volume model in a group and commit it
    this->oGroup = ospNewGroup();
    OSPData oModelData = ospNewSharedData1D(&this->oModel, OSP_VOLUMETRIC_MODEL, 1);
    ospSetObject(this->oGroup, "volume", oModelData);
    ospCommit(this->oGroup);

    // put the group in an instance and commit it
    this->oInstance = ospNewInstance(this->oGroup);
    ospCommit(this->oInstance);

    // put the instance in the world
    this->oWorld = ospNewWorld();
    OSPData oInstanceData = ospNewSharedData1D(&this->oInstance, OSP_INSTANCE, 1);
    ospSetObject(this->oWorld, "instance", oInstanceData);
    ospCommit(this->oWorld);
}

void Renderer::addLight()
{
    // currently the renderer will hold only one light
    if(this->lights.size() == 0) {
        // create a new directional light
        OSPLight light = ospNewLight("distant");
        // OSPLight light = ospNewLight(this->oRenderer, "distant");
        //float direction[] = {0, -1, 1};
        //ospSetVec3f(light, "direction", direction);
        // set the apparent size of the light in degrees
        // 0.53 approximates the Sun
        ospSetFloat(light, "angularDiameter", 0.53);
        ospCommit(light);
        this->lights.push_back(light);
    }
}

void Renderer::setIsosurface(Volume *v, std::vector<float> &isoValues)
{
    this->setIsosurface(v, isoValues, 0.1);
}

void Renderer::setIsosurface(Volume *v, std::vector<float> &isoValues,
        float specular)
{
    if(this->lastVolumeID == v->ID && this->lastRenderType == "isosurface") {
        // this is the same volume as the current model and we previously
        // did an isosurface render

        // but check if the isoValues are different
        if(this->lastIsoValues == isoValues) {
            return;
        }
    }
    if(this->oWorld != NULL) {
        ospRelease(this->oModel);
        ospRelease(this->oGroup);
        ospRelease(this->oInstance);
        ospRelease(this->oWorld);

        this->oModel = NULL;
        this->oGroup = NULL;
        this->oInstance = NULL;
        this->oWorld = NULL;
    }

    // set up light and material if necessary
    this->addLight();
    if(this->oMaterial == NULL) {
        // create a new surface material with some specular highlighting
        // this->oMaterial = ospNewMaterial(this->oRenderer, "OBJMaterial");
        this->oMaterial = ospNewMaterial(nullptr, "OBJMaterial");
        // float Ks[] = {specular, specular, specular};
        // float Kd[] = {1.f-specular, 1.f-specular, 1.f-specular};
        ospSetVec3f(this->oMaterial, "Kd", 1.f-specular, 1.f-specular, 1.f-specular);
        ospSetVec3f(this->oMaterial, "Ks", specular, specular, specular);
        ospSetFloat(this->oMaterial, "Ns", 10);
        ospCommit(this->oMaterial);
    }

    // create an isosurface object
    if(this->oSurface != NULL) {
        ospRelease(this->oSurface);
        this->oSurface = NULL;
    }
    this->oSurface = ospNewGeometry("isosurfaces");
    // OSPData isoValuesDataArray = ospNewData(isoValues.size(), OSP_FLOAT,
    //         isoValues.data());
    OSPData isoValuesDataArray = ospNewSharedData(isoValues.data(),  OSP_FLOAT,
            isoValues.size());
    ospSetObject(this->oSurface, "isovalues", isoValuesDataArray);
    ospSetObject(this->oSurface, "volume", v->asOSPRayObject());
    ospSetObject(this->oSurface, "material", this->oMaterial);
    // ospSetMaterial(this->oSurface, this->oMaterial);
    ospCommit(this->oSurface);

    this->lastVolumeID = v->ID;
    this->lastRenderType = "isosurface";
    this->lastIsoValues = isoValues;
    // this->oModel = 
    // this->oModel = ospNewModel();
    // ospAddGeometry(this->oModel, this->oSurface);
    // ospCommit(this->oModel);


    // put the geometry in a model and commit it
    this->oModel = ospNewGeometricModel(this->oSurface);
    ospCommit(this->oModel);

    // put the volume model in a group and commit it
    this->oGroup = ospNewGroup();
    OSPData oModelData = ospNewSharedData1D(&this->oModel, OSP_GEOMETRIC_MODEL, 1);
    ospSetObject(this->oGroup, "geometry", oModelData);
    ospCommit(this->oGroup);

    // put the group in an instance and commit it
    this->oInstance = ospNewInstance(this->oGroup);
    ospCommit(this->oInstance);

    // put the instance in the world
    this->oWorld = ospNewWorld();
    OSPData oInstanceData = ospNewSharedData1D(&this->oInstance, OSP_INSTANCE, 1);
    ospSetObject(this->oWorld, "instance", oInstanceData);
    ospCommit(this->oWorld);
}

void Renderer::setCamera(Camera *c)
{
    if(this->lastCameraID == c->ID) {
        // this is the same camera as the current one
        return;
    }
    if(this->oCamera != NULL) {
        //std::cerr << "Camera already set!" << std::endl;
        //return;
        ospRelease(this->oCamera);
        this->oCamera = NULL;
    }

    this->lastCameraID = c->ID;
    this->cameraWidth = c->getImageWidth();
    this->cameraHeight = c->getImageHeight();
    // grab the light direction while we have the pbnj Camera
    this->lightDirection[0] = c->viewX;
    this->lightDirection[1] = c->viewY;
    this->lightDirection[2] = c->viewZ;
    this->oCamera = c->asOSPRayObject();
    this->pbnjCamera = c;
}

void Renderer::setSamples(unsigned int spp)
{
    this->samples = spp;
    ospSetInt(this->oRenderer, "spp", spp);
    ospCommit(this->oRenderer);
}

void Renderer::renderImage(std::string imageFilename)
{
    IMAGETYPE imageType = this->getFiletype(imageFilename);
    if(imageType == INVALID) {
        std::cerr << "Invalid image filetype requested!" << std::endl;
        return;
    }

    this->saveImage(imageFilename, imageType);
}

void Renderer::renderToJPGObject(std::vector<unsigned char> &jpg, int quality)
{
    unsigned char *colorBuffer;
    this->renderToBuffer(&colorBuffer);

    // CImg doesn't interlace the channels, we have to work around that 
    cimg_library::CImg<unsigned char> img(colorBuffer, 4, this->cameraWidth, 
            this->cameraHeight, 1, false);
    img.permute_axes("yzcx");

    img.save_jpeg_to_memory(jpg, quality);
    free(colorBuffer);
}

void Renderer::renderToPNGObject(std::vector<unsigned char> &png)
{
    unsigned char *colorBuffer;
    this->renderToBuffer(&colorBuffer);
    unsigned int error = lodepng::encode(png, colorBuffer,
            this->cameraWidth, this->cameraHeight);
    if(error) {
        std::cerr << "ERROR: could not encode PNG, error " << error << ": ";
        std::cerr << lodepng_error_text(error) << std::endl;
    }
    free(colorBuffer);
}

/*
 * Renders the OSPRay buffer to buffer and sets the width and height in 
 * their respective variables.
 */
void Renderer::renderToBuffer(unsigned char **buffer)
{
    this->render();
    int width = this->cameraWidth;
    int height = this->cameraHeight;
    uint32_t *colorBuffer = (uint32_t *)ospMapFrameBuffer(this->oFrameBuffer,
            OSP_FB_COLOR);
    
    *buffer = (unsigned char *) malloc(4 * width * height);
    
    for(int j = 0; j < height; j++) {
        unsigned char *rowIn = (unsigned char*)&colorBuffer[(height-1-j)*width];
        for(int i = 0; i < width; i++) {
            int index = j * width + i;
            // composite rowIn RGB with background color
            float a = rowIn[4*i + 3] / 255.0;
            float r = rowIn[4*i + 0] * a,
                  g = rowIn[4*i + 1] * a,
                  b = rowIn[4*i + 2] * a;
            float abg = this->backgroundColor[3] / 255.0;
            float rbg = this->backgroundColor[0] * abg,
                  gbg = this->backgroundColor[1] * abg,
                  bbg = this->backgroundColor[2] * abg;

            (*buffer)[4*index + 0] = (unsigned char) (r + rbg * (1 - a));
            (*buffer)[4*index + 1] = (unsigned char) (g + gbg * (1 - a));
            (*buffer)[4*index + 2] = (unsigned char) (b + bbg * (1 - a));
            (*buffer)[4*index + 3] = (unsigned char) 255 * (a + abg * (1 - a));
        }
    }

    ospUnmapFrameBuffer(colorBuffer, this->oFrameBuffer);
    ospRelease(this->oFrameBuffer);
}

void Renderer::render()
{
    //check if everything is ready for rendering
    bool exit = false;
    if(this->oModel == NULL) {
        std::cerr << "No volume set to render!" << std::endl;
        exit = true;
    }
    if(this->oCamera == NULL) {
        std::cerr << "No camera set to render with!" << std::endl;
        exit = true;
    }
    if(exit)
        return;

    //finalize the OSPRay renderer
    if(this->lights.size() == 1) {
        //if there was a light, set its direction based on the camera
        //and add it to the renderer
        ospSetVec3f(this->lights[0], "direction", 
            this->lightDirection[0],this->lightDirection[1],this->lightDirection[2]);
        ospCommit(this->lights[0]);

         OSPData lightDataArray = ospNewSharedData(this->lights.data(),  OSP_LIGHT, this->lights.size());
        // OSPData lightDataArray = ospNewData(this->lights.size(), OSP_LIGHT, this->lights.data());
        ospCommit(lightDataArray);
        ospSetObject(this->oRenderer, "lights", lightDataArray);
        unsigned int aoSamples = std::max(this->samples/8, (unsigned int) 1);
        ospSetInt(this->oRenderer, "aoSamples", aoSamples);
        ospSetInt(this->oRenderer, "shadowsEnabled", 0);
        ospSetInt(this->oRenderer, "oneSidedLighting", 0);
    }
    ospSetObject(this->oRenderer, "model", this->oModel);
    ospSetObject(this->oRenderer, "camera", this->oCamera);
    ospCommit(this->oRenderer);

    //set up framebuffer
    // osp::vec2i imageSize;
    this->cameraWidth = this->pbnjCamera->getImageWidth();
    this->cameraHeight = this->pbnjCamera->getImageHeight();
    // imageSize.x = this->cameraWidth;
    // imageSize.y = this->cameraHeight;
    //this framebuffer will be released after a single frame
    this->oFrameBuffer = ospNewFrameBuffer(this->cameraWidth, this->cameraHeight, OSP_FB_SRGBA,
                                           OSP_FB_COLOR | OSP_FB_ACCUM);
    // this->oFrameBuffer = ospNewFrameBuffer(imageSize, OSP_FB_SRGBA,
    //                                        OSP_FB_COLOR | OSP_FB_ACCUM);

    // OSPWorld world = ospNewWorld();
    // OSPData instances = ospNewSharedData1D(&instance, OSP_INSTANCE, 1);
    // ospSetObject(world, "instance", instances);
    // ospCommit(world);
    ospRenderFrame(this->oFrameBuffer, this->oRenderer, this->oCamera, this->oWorld);
    // ospRenderFrame(this->oFrameBuffer, this->oRenderer,
    //             OSP_FB_COLOR | OSP_FB_ACCUM);
}

IMAGETYPE Renderer::getFiletype(std::string filename)
{
    std::stringstream ss;
    ss.str(filename);
    std::string token;
    char delim = '.';
    while(std::getline(ss, token, delim)) {
    }

    if(token.compare("ppm") == 0) {
        return PIXMAP;
    }
    else if(token.compare("png") == 0) {
        return PNG;
    }
    else if(token.compare("jpg") == 0 || token.compare("jpeg") == 0) {
        return JPG;
    }
    else {
        return INVALID;
    }
}

void Renderer::saveImage(std::string filename, IMAGETYPE imageType)
{
    if(imageType == PIXMAP)
        this->saveAsPPM(filename);
    else if(imageType == PNG)
        this->saveAsPNG(filename);
    else if (imageType == JPG)
        this->saveAsJPG(filename);
}

void Renderer::saveAsPPM(std::string filename)
{
    this->render();
    int width = this->cameraWidth, height = this->cameraHeight;
    uint32_t *colorBuffer = (uint32_t *)ospMapFrameBuffer(this->oFrameBuffer,
            OSP_FB_COLOR);
    //do a binary file so the PPM isn't quite so large
    FILE *file = fopen(filename.c_str(), "wb");
    unsigned char *rowOut = (unsigned char *)malloc(3*width);
    fprintf(file, "P6\n%i %i\n255\n", width, height);

    //the OSPRay framebuffer uses RGBA, but PPM only supports RGB
    for(int j = 0; j < height; j++) {
        unsigned char *rowIn = (unsigned char*)&colorBuffer[(height-1-j)*width];
        for(int i = 0; i < width; i++) {
            // composite rowIn RGB with background color
            unsigned char r = rowIn[4*i + 0],
                          g = rowIn[4*i + 1],
                          b = rowIn[4*i + 2];
            float a = rowIn[4*i + 3] / 255.0;
            rowOut[3*i + 0] = (unsigned char) r * a + 
                this->backgroundColor[0] * (1.0-a);
            rowOut[3*i + 1] = (unsigned char) g * a + 
                this->backgroundColor[1] * (1.0-a);
            rowOut[3*i + 2] = (unsigned char) b * a + 
                this->backgroundColor[2] * (1.0-a);
        }
        fwrite(rowOut, 3*width, sizeof(char), file);
    }

    fprintf(file, "\n");
    fclose(file);

    //unmap and release so OSPRay will deallocate the memory
    //used by the framebuffer
    ospUnmapFrameBuffer(colorBuffer, this->oFrameBuffer);
    ospRelease(this->oFrameBuffer);
}

void Renderer::saveAsPNG(std::string filename)
{
    std::vector<unsigned char> converted_image;
    this->renderToPNGObject(converted_image);
    //write to file
    lodepng::save_file(converted_image, filename.c_str());
}

void Renderer::saveAsJPG(std::string filename)
{
    unsigned char *colorBuffer;
    this->renderToBuffer(&colorBuffer);

    // CImg doesn't interlace the channels, we have to work around that 
    cimg_library::CImg<unsigned char> img(colorBuffer, 4, this->cameraWidth, 
            this->cameraHeight, 1, false);
    img.permute_axes("yzcx");

    img.save(filename.c_str());
}

}

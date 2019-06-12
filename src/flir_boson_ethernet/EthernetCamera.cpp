#include "flir_boson_ethernet/EthernetCamera.h"

using namespace cv;
using namespace flir_boson_ethernet;

gcstring GetDottedAddress( int64_t value )
{
    // Helper function for formatting IP Address into the following format
    // x.x.x.x
    unsigned int inputValue = static_cast<unsigned int>( value );
    ostringstream convertValue;
    convertValue << ((inputValue & 0xFF000000) >> 24);
    convertValue << ".";
    convertValue << ((inputValue & 0x00FF0000) >> 16);
    convertValue << ".";
    convertValue << ((inputValue & 0x0000FF00) >> 8);
    convertValue << ".";
    convertValue << (inputValue & 0x000000FF);
    return convertValue.str().c_str();
}

EthernetCamera::EthernetCamera(string ip, string camInfoPath) :
    _ipAddr(ip),     
{

}
EthernetCamera::~EthernetCamera() {
    delete _pCam;
    delete _bufferStart;
    closeCamera();
}

// AGC Sample ONE: Linear from min to max.
// Input is a MATRIX (height x width) of 16bits. (OpenCV mat)
// Output is a MATRIX (height x width) of 8 bits (OpenCV mat)
void EthernetCamera::agcBasicLinear(const Mat &input_16,
                                    Mat *output_8,
                                    const int &height,
                                    const int &width)
{
}

bool EthernetCamera::openCamera()
{
    _system = System::GetInstance();
    CameraList camList = _system->GetCameras();
    const unsigned int numCameras = camList.GetSize();

    if(numCameras == 0) {
        ROS_ERROR("flir_boson_ethernet - ERROR : NO_CAMERAS. No cameras found");
        camList.Clear();
        _system->ReleaseInstance();
        return false;
    }

    findMatchingCamera(camList, numCameras);
    
    if(!_pCam.IsValid()) {
        ROS_ERROR("flir_boson_ethernet - ERROR : OPEN. No device matches ip_addr: %s", _ipAddr.c_str());
        return false;
    }
    _pCam->Init();
    setCameraEvents();

    if (!setImageAcquisition())
    {
        ROS_ERROR("flir_boson_ethernet - ERROR : CAMERA_ACQUISITION. Cannot set image acquisition.");
        return false;
    }

    if(!setImageInfo()) {
        ROS_ERROR("flir_boson_ethernet - ERROR : GET_CONFIGURATION. Cannot get image for setting dimensions");
        return false;
    }

    initOpenCVBuffers();
    setCameraInfo();

    return true;
} 

void EthernetCamera::setCameraEvents() {
    _imageHandler = new ImageEventHandler(_pCam);
    _pCam->RegisterEvent(*_imageHandler);
}

bool EthernetCamera::setImageInfo() {
    try {
        auto imgInfo = _imageHandler->GetImageInfo();
        _width = imgInfo.width;
        _height = imgInfo.height;
        _imageSize = imgInfo.size;
        _bufferStart = new uint8_t[_imageSize];
        ROS_INFO("Camera info - Width: %d, Height: %d", _width, _height);

        return true;
    } catch(Spinnaker::Exception e) {
        ROS_ERROR("flir_boson_ethernet - ERROR : %s", e.what());
        return false;
    }
}

void EthernetCamera::findMatchingCamera(CameraList camList, const unsigned int numCams) {
    gcstring deviceIPAddress = "0.0.0.0";

    for (unsigned int i = 0; i < numCams; i++)
    {
        // Select camera
        CameraPtr cam = camList.GetByIndex(i);
        INodeMap &nodeMapTLDevice = cam->GetTLDeviceNodeMap();

        CIntegerPtr ptrIPAddress = nodeMapTLDevice.GetNode("GevDeviceIPAddress");
        if (IsAvailable(ptrIPAddress) && IsReadable(ptrIPAddress)) {
            deviceIPAddress = GetDottedAddress(ptrIPAddress->GetValue());
        }
        if(deviceIPAddress == _ipAddr) {
            CStringPtr modelName = nodeMapTLDevice.GetNode("DeviceModelName");
            ROS_INFO("Found matching camera %s", modelName->ToString().c_str());
            _pCam = cam;
            return;
        }
    }
}

bool EthernetCamera::setImageAcquisition() {
    INodeMap &nodeMapTLDevice = _pCam->GetTLDeviceNodeMap();
    INodeMap &nodeMap = _pCam->GetNodeMap();

    CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");
    if (!IsAvailable(ptrAcquisitionMode) || !IsWritable(ptrAcquisitionMode)) 
    {
        ROS_ERROR("Unable to set acquisition mode. Aborting...");
        return false;
    }

    // Retrieve entry node from enumeration node
    CEnumEntryPtr ptrAcquisitionModeContinuous = ptrAcquisitionMode->GetEntryByName("Continuous");
    if (!IsAvailable(ptrAcquisitionModeContinuous) || !IsReadable(ptrAcquisitionModeContinuous))
    {
        ROS_ERROR("Unable to set acquisition mode to continuous (entry retrieval). Aborting...");
        return false;
    }

    // Retrieve integer value from entry node
    const int64_t acquisitionModeContinuous = ptrAcquisitionModeContinuous->GetValue();

    // Set integer value from entry node as new value of enumeration node
    ptrAcquisitionMode->SetIntValue(acquisitionModeContinuous);

    ROS_INFO("Acquisition mode set to continuous...");
    
    _pCam->BeginAcquisition();

    return true;
}

void EthernetCamera::initOpenCVBuffers() {
    // Declarations for Zoom representation
    // Will be used or not depending on program arguments
    // OpenCV output buffer , BGR -> Three color spaces :
    // (640 - 640 - 640 : p11 p21 p31 .... / p12 p22 p32 ..../ p13 p23 p33 ...)
    _thermalImageMat = Mat(_height, _width, CV_8UC3, 
        reinterpret_cast<void *>(_bufferStart));
}

void EthernetCamera::setCameraInfo() {
    INodeMap &nodeMapTLDevice = _pCam->GetTLDeviceNodeMap();
    CStringPtr modelName = nodeMapTLDevice.GetNode("DeviceModelName");

    _cameraInfo->setCameraName(modelName->GetValue().c_str());
    if (_cameraInfo->validateURL(camera_info_url)) {
        _cameraInfo->loadCameraInfo(camera_info_url);
    } else {
        ROS_INFO("flir_boson_ethernet - camera_info_url could not be validated. Publishing with unconfigured camera.");
    }
}

bool EthernetCamera::closeCamera()
{
    if(_pCam != nullptr) {
        unsetCameraEvents();
        _pCam->EndAcquisition();
        _pCam->DeInit();
    }

    return true;
}

void EthernetCamera::unsetCameraEvents() {
    _pCam->UnregisterEvent(*_imageHandler);
    delete _imageHandler;
}


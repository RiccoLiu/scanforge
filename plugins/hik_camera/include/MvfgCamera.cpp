//#include "pch.h"

#include "MvfgCamera.h"
#pragma comment(lib,"..\\LBAS\\Libraries\\win64\\MVFGControl.lib")

//IFHANDLE CMvfgCamera::m_hInterface = MV_NULL;

CMvfgCamera::CMvfgCamera()
{
    //m_hInterface = MV_NULL;
    m_hDevice = MV_NULL;
    m_hStream = MV_NULL;
}

CMvfgCamera::~CMvfgCamera()
{
    //if (m_hInterface)
    //{
    //    m_hInterface = MV_NULL;
    //}
    if (m_hDevice)
    {
        m_hDevice = MV_NULL;
    }
    if (m_hStream)
    {
        m_hStream = MV_NULL;
    }
}

// ch:获取SDK版本号 | en:Get SDK Version
//int CMvfgCamera::GetSDKVersion()
//{
//    return MV_FG_GetSDKVersion();
//}


// ch:枚举采集卡 | en:Enumerate Interface
int CMvfgCamera::EnumInterfaces(unsigned int* pnNumInterfaces)
{
    int          nRet = 0;
    unsigned int nDeviceNum = 0;

    // ch:枚举采集卡 | en:Enum interface
    bool bChanged = false;
    nRet = MV_FG_UpdateInterfaceList(MV_FG_CXP_INTERFACE | MV_FG_GEV_INTERFACE | MV_FG_CAMERALINK_INTERFACE | MV_FG_XoF_INTERFACE, &bChanged);
    if (MV_FG_SUCCESS != nRet)
    {
        //ShowErrorMsg(_TEXT("Enum devices failed"), nRet);
        return nRet;
    }

    // ch:获取设备数量 | en:Get device number
    nRet = MV_FG_GetNumInterfaces(pnNumInterfaces);
    if (MV_FG_SUCCESS != nRet)
    {
        //ShowErrorMsg(_TEXT("Get device number failed"), nRet);
        return nRet;
    }
    if (0 == *pnNumInterfaces)
    {
        //ShowErrorMsg(_TEXT("No Interface"), 0);
        return nRet;
    }

    unsigned int nInterfaceIndex = 0;
    // ch:打开采集卡，获得采集卡句柄 | en:Open interface, get handle
    nRet = MV_FG_OpenInterface(nInterfaceIndex, &m_hInterface);
    if (MV_FG_SUCCESS != nRet)
    {
        //ShowErrorMsg(_TEXT("Open interface failed"), nRet);
        return nRet;
    }

    // ch:开启采集卡上的分离方案使能 | en:Enable seperate scheme on interface.
    //nRet = MV_FG_SetBoolValue(m_hInterface, "CameraDeviceEnable", true);
    //if (MV_FG_SUCCESS != nRet)
    //{
    //    MV_FG_CloseInterface(m_hInterface);
    //    m_hInterface = NULL;
    //    //ShowErrorMsg(_TEXT("Set CameraDeviceEnable failed"), nRet);
    //    return nRet;
    //}

    return nRet;
}

// ch:打开采集卡 | en:Open Interface
int CMvfgCamera::OpenIF()
{
    int nRet = 0;

    // ch:获取采集卡数量 | en:Get interface num
    unsigned int nInterfaceNum = 0;
    nRet = MV_FG_GetNumInterfaces(&nInterfaceNum);

    // ch:打开采集卡，获得采集卡句柄 | en:Open interface, get handle
    unsigned int nInterfaceIndex = 0;
    nRet = MV_FG_OpenInterface(nInterfaceIndex, &m_hInterface);
    if (MV_FG_SUCCESS != nRet)
    {
        //ShowErrorMsg(_TEXT("Open interface failed"), nRet);
        m_hInterface = MV_NULL;
        return nRet;
    }

    return nRet;
}

// ch:关闭采集卡 | en:Close Interface
int CMvfgCamera::CloseIF()
{
    int nRet = 0;

    if (m_hInterface)
    {
        // ch:关闭采集卡上的分离方案使能 | en:Disable seperate scheme on interface.
        //MV_FG_SetBoolValue(m_hInterface, "CameraDeviceEnable", false);

        nRet = MV_FG_CloseInterface(m_hInterface);
        if (MV_FG_SUCCESS != nRet)
        {
            //ShowErrorMsg(_TEXT("Close interface failed"), nRet);
        }
        m_hInterface = NULL;
        return nRet;
    }

    return nRet;
}


// ch:枚举设备 | en:Enumerate Device
int CMvfgCamera::EnumDevices(unsigned int* pnNumDevices)
{
    int          nRet = 0;
    unsigned int nDeviceNum = 0;

    if (!m_bChanged)
    {
        // ch:枚举采集卡 | en:Enum interface
        nRet = MV_FG_UpdateInterfaceList(MV_FG_CXP_INTERFACE | MV_FG_GEV_INTERFACE | MV_FG_CAMERALINK_INTERFACE | MV_FG_XoF_INTERFACE, &m_bChanged);
        if (MV_FG_SUCCESS != nRet)
        {
            //ShowErrorMsg(_TEXT("Enum Interfacs failed"), nRet);
            return nRet;
        }
    }

    if (m_hInterface == MV_NULL)
    {
        // ch:获取采集卡数量 | en:Get interface num
        unsigned int nInterfaceNum = 0;
        nRet = MV_FG_GetNumInterfaces(&nInterfaceNum);
        if (MV_FG_SUCCESS != nRet)
        {
            //ShowErrorMsg(_TEXT("Get Interfacs number failed"), nRet);
            return nRet;
        }
        if (0 == nInterfaceNum)
        {
            //ShowErrorMsg(_TEXT("No Interface"), 0);
            return nRet;
        }

        unsigned int nInterfaceIndex = 0;
        // ch:打开采集卡，获得采集卡句柄 | en:Open interface, get handle
        nRet = MV_FG_OpenInterface(nInterfaceIndex, &m_hInterface);
        if (MV_FG_SUCCESS != nRet)
        {
            //ShowErrorMsg(_TEXT("Open interface failed"), nRet);
            return nRet;
        }

        // ch:开启采集卡上的分离方案使能 | en:Enable seperate scheme on interface.
        //nRet = MV_FG_SetBoolValue(m_hInterface, "CameraDeviceEnable", true);
        //if (MV_FG_SUCCESS != nRet)
        //{
        //    MV_FG_CloseInterface(m_hInterface);
        //    m_hInterface = NULL;
        //    //ShowErrorMsg(_TEXT("Set CameraDeviceEnable failed"), nRet);
        //    return nRet;
        //}

    }

    // ch:枚举采集卡上的相机 | en:Enum camera of interface
    bool bChanged = false;
    nRet = MV_FG_UpdateDeviceList(m_hInterface, &bChanged);
    if (MV_FG_SUCCESS != nRet)
    {
        //ShowErrorMsg(_TEXT("Enum devices failed"), nRet);
        return nRet;
    }

    // ch:获取设备数量 | en:Get device number
    nRet = MV_FG_GetNumDevices(m_hInterface, pnNumDevices);
    if (MV_FG_SUCCESS != nRet)
    {
        //ShowErrorMsg(_TEXT("Get device number failed"), nRet);
        return nRet;
    }

    return nRet;
}

// ch:判断设备是否可达 | en:Is the device accessible
//bool CMvfgCamera::IsDeviceAccessible(MV_FG_DEVICE_INFO* pstDevInfo, unsigned int nAccessMode)
//{
//    return MV_CC_IsDeviceAccessible(pstDevInfo, nAccessMode);
//}

// ch:打开设备 | en:Open Device
int CMvfgCamera::Open()
{
    int nRet = 0;

    if (MV_NULL == m_hInterface)
    {
        return MV_FG_ERR_INVALID_HANDLE;
    }

    if (m_hDevice)
    {
        return MV_FG_ERR_CALLORDER;
    }

    // ch:打开设备，获得设备句柄 | en:Open device, get handle
    unsigned int nIndex = 0;
    nRet = MV_FG_OpenDevice(m_hInterface, nIndex, &m_hDevice);
    if (MV_FG_SUCCESS != nRet)
    {
        m_hDevice = MV_NULL;
        return nRet;
    }

    return nRet;
}

// ch:关闭设备 | en:Close Device
int CMvfgCamera::Close()
{
    int nRet = 0;

    if (m_hStream)
    {
        nRet = MV_FG_CloseStream(m_hStream);
        if (MV_FG_SUCCESS != nRet)
        {
            //ShowErrorMsg(TEXT("Close stream Failed"), nRet);
        }
        m_hStream = NULL;
    }

    if (m_hDevice)
    {
        nRet = MV_FG_CloseDevice(m_hDevice);
        if (MV_FG_SUCCESS != nRet)
        {
            //ShowErrorMsg(TEXT("Close device Failed"), nRet);
        }
        m_hDevice = NULL;
    }

    // 关闭采集卡
    //CloseIF();

    return nRet;
}

// ch:判断相机是否处于连接状态 | en:Is The Device Connected
//bool CMvfgCamera::IsDeviceConnected()
//{
//    return MV_CC_IsDeviceConnected(m_hDevHandle);
//}

// ch:注册图像数据回调 | en:Register Image Data CallBack
//int CMvfgCamera::RegisterImageCallBack(void(__stdcall* cbOutput)(unsigned char * pData, MV_FRAME_OUT_INFO_EX* pFrameInfo, void* pUser), void* pUser)
//{
//    return MV_CC_RegisterImageCallBackEx(m_hDevHandle, cbOutput, pUser);
//}

//获取流通道数量
int CMvfgCamera::GetNumStreams(unsigned int* pnNumStreams)
{
    return MV_FG_GetNumStreams(m_hDevice, pnNumStreams);
}

//打开流通道
int CMvfgCamera::OpenStream(unsigned int nIndex)
{
    return MV_FG_OpenStream(m_hDevice, nIndex, &m_hStream);
}
//关闭流通道
int CMvfgCamera::CloseStream()
{
    return MV_FG_CloseStream(m_hStream);
}

//设置SDK内部缓存数量
int CMvfgCamera::SetBufferNum(unsigned int nBufferNum)
{
    return MV_FG_SetBufferNum(m_hStream, nBufferNum);
}

// ch:开启抓图 | en:Start Grabbing
int CMvfgCamera::StartGrabbing()
{
    return MV_FG_StartAcquisition(m_hStream);
}

// ch:停止抓图 | en:Stop Grabbing
int CMvfgCamera::StopGrabbing()
{
    return MV_FG_StopAcquisition(m_hStream);
}

// ch:主动获取一帧图像数据 | en:Get one frame initiatively
int CMvfgCamera::GetImageBuffer(MV_FG_BUFFER_INFO* pFrame, int nMsec)
{
    return MV_FG_GetFrameBuffer(m_hStream, pFrame, nMsec);
}

// ch:释放图像缓存 | en:Free image buffer
int CMvfgCamera::FreeImageBuffer(MV_FG_BUFFER_INFO* pFrame)
{
    return MV_FG_ReleaseFrameBuffer(m_hStream, pFrame);
}

// ch:设置显示窗口句柄 | en:Set Display Window Handle
int CMvfgCamera::DisplayOneFrame(void* hWnd, MV_FG_DISPLAY_FRAME_INFO* pDisplayInfo)
{
    return MV_FG_DisplayOneFrame(m_hStream, hWnd, pDisplayInfo);
}

// ch:设置SDK内部图像缓存节点个数 | en:Set the number of the internal image cache nodes in SDK
//int CMvfgCamera::SetImageNodeNum(unsigned int nNum)
//{
//    return MV_CC_SetImageNodeNum(m_hDevHandle, nNum);
//}

// ch:获取设备信息 | en:Get device information
int CMvfgCamera::GetDeviceInfo(unsigned int nIndex, MV_FG_DEVICE_INFO* pstDevInfo)
{
    return MV_FG_GetDeviceInfo(m_hInterface, nIndex, pstDevInfo);
}

// ch:获取GEV相机的统计信息 | en:Get detect info of GEV camera
//int CMvfgCamera::GetGevAllMatchInfo(MV_MATCH_INFO_NET_DETECT* pMatchInfoNetDetect)
//{
//    if (MV_NULL == pMatchInfoNetDetect)
//    {
//        return MV_E_PARAMETER;
//    }
//
//    MV_CC_DEVICE_INFO stDevInfo = {0};
//    GetDeviceInfo(&stDevInfo);
//    if (stDevInfo.nTLayerType != MV_GIGE_DEVICE)
//    {
//        return MV_E_SUPPORT;
//    }
//
//    MV_ALL_MATCH_INFO struMatchInfo = {0};
//
//    struMatchInfo.nType = MV_MATCH_TYPE_NET_DETECT;
//    struMatchInfo.pInfo = pMatchInfoNetDetect;
//    struMatchInfo.nInfoSize = sizeof(MV_MATCH_INFO_NET_DETECT);
//    memset(struMatchInfo.pInfo, 0, sizeof(MV_MATCH_INFO_NET_DETECT));
//
//    return MV_CC_GetAllMatchInfo(m_hDevHandle, &struMatchInfo);
//}

// ch:获取U3V相机的统计信息 | en:Get detect info of U3V camera
//int CMvfgCamera::GetU3VAllMatchInfo(MV_MATCH_INFO_USB_DETECT* pMatchInfoUSBDetect)
//{
//    if (MV_NULL == pMatchInfoUSBDetect)
//    {
//        return MV_E_PARAMETER;
//    }
//
//    MV_CC_DEVICE_INFO stDevInfo = {0};
//    GetDeviceInfo(&stDevInfo);
//    if (stDevInfo.nTLayerType != MV_USB_DEVICE)
//    {
//        return MV_E_SUPPORT;
//    }
//
//    MV_ALL_MATCH_INFO struMatchInfo = {0};
//
//    struMatchInfo.nType = MV_MATCH_TYPE_USB_DETECT;
//    struMatchInfo.pInfo = pMatchInfoUSBDetect;
//    struMatchInfo.nInfoSize = sizeof(MV_MATCH_INFO_USB_DETECT);
//    memset(struMatchInfo.pInfo, 0, sizeof(MV_MATCH_INFO_USB_DETECT));
//    
//    return MV_CC_GetAllMatchInfo(m_hDevHandle, &struMatchInfo);
//}

// ch:获取和设置Int型参数，如 Width和Height，详细内容参考SDK安装目录下的 MvCameraNode.xlsx 文件
// en:Get Int type parameters, such as Width and Height, for details please refer to MvCameraNode.xlsx file under SDK installation directory
int CMvfgCamera::GetIntValue(IN int iPortType, IN const char* strKey, OUT MV_FG_INTVALUE* pIntValue)
{
    if (iPortType == MV_FG_DATA_SOURCE_INTERFACE)
    {
        return MV_FG_GetIntValue(m_hInterface, strKey, pIntValue);
    }
    else if (iPortType == MV_FG_DATA_SOURCE_DEVICE)
    {
        return MV_FG_GetIntValue(m_hDevice, strKey, pIntValue);
    }

    //return MV_FG_GetIntValue(hPort, strKey, pIntValue);
}

int CMvfgCamera::SetIntValue(IN int iPortType, IN const char* strKey, IN int64_t nValue)
{
    if (iPortType == MV_FG_DATA_SOURCE_INTERFACE)
    {
        return MV_FG_SetIntValue(m_hInterface, strKey, nValue);
    }
    else if (iPortType == MV_FG_DATA_SOURCE_DEVICE)
    {
        return MV_FG_SetIntValue(m_hDevice, strKey, nValue);
    }

    //return MV_FG_SetIntValue(hPort, strKey, nValue);
}

// ch:获取和设置Enum型参数，如 PixelFormat，详细内容参考SDK安装目录下的 MvCameraNode.xlsx 文件
// en:Get Enum type parameters, such as PixelFormat, for details please refer to MvCameraNode.xlsx file under SDK installation directory
int CMvfgCamera::GetEnumValue(IN int iPortType, IN const char* strKey, OUT MV_FG_ENUMVALUE* pEnumValue)
{
    if (iPortType == MV_FG_DATA_SOURCE_INTERFACE)
    {
        return MV_FG_GetEnumValue(m_hInterface, strKey, pEnumValue);
    }
    else if (iPortType == MV_FG_DATA_SOURCE_DEVICE)
    {
        return MV_FG_GetEnumValue(m_hDevice, strKey, pEnumValue);
    }

    //return MV_FG_GetEnumValue(hPort, strKey, pEnumValue);
}

int CMvfgCamera::SetEnumValue(IN int iPortType, IN const char* strKey, IN unsigned int nValue)
{
    if (iPortType == MV_FG_DATA_SOURCE_INTERFACE)
    {
        return MV_FG_SetEnumValue(m_hInterface, strKey, nValue);
    }
    else if (iPortType == MV_FG_DATA_SOURCE_DEVICE)
    {
        return MV_FG_SetEnumValue(m_hDevice, strKey, nValue);
    }

    //return MV_FG_SetEnumValue(hPort, strKey, nValue);
}

int CMvfgCamera::SetEnumValueByString(IN int iPortType, IN const char* strKey, IN const char* sValue)
{
    if (iPortType == MV_FG_DATA_SOURCE_INTERFACE)
    {
        return MV_FG_SetEnumValueByString(m_hInterface, strKey, sValue);
    }
    else if (iPortType == MV_FG_DATA_SOURCE_DEVICE)
    {
        return MV_FG_SetEnumValueByString(m_hDevice, strKey, sValue);
    }

    //return MV_FG_SetEnumValueByString(hPort, strKey, sValue);
}

//int CMvfgCamera::GetEnumEntrySymbolic(IN const char* strKey, IN MVCC_ENUMENTRY* pstEnumEntry)
//{
//    return MV_CC_GetEnumEntrySymbolic(m_hDevHandle, strKey, pstEnumEntry);
//}

// ch:获取和设置Float型参数，如 ExposureTime和Gain，详细内容参考SDK安装目录下的 MvCameraNode.xlsx 文件
// en:Get Float type parameters, such as ExposureTime and Gain, for details please refer to MvCameraNode.xlsx file under SDK installation directory
int CMvfgCamera::GetFloatValue(IN int iPortType, IN const char* strKey, OUT MV_FG_FLOATVALUE* pFloatValue)
{
    if (iPortType == MV_FG_DATA_SOURCE_INTERFACE)
    {
        return MV_FG_GetFloatValue(m_hInterface, strKey, pFloatValue);
    }
    else if (iPortType == MV_FG_DATA_SOURCE_DEVICE)
    {
        return MV_FG_GetFloatValue(m_hDevice, strKey, pFloatValue);
    }

    //return MV_FG_GetFloatValue(hPort, strKey, pFloatValue);
}

int CMvfgCamera::SetFloatValue(IN int iPortType, IN const char* strKey, IN float fValue)
{
    if (iPortType == MV_FG_DATA_SOURCE_INTERFACE)
    {
        return MV_FG_SetFloatValue(m_hInterface, strKey, fValue);
    }
    else if (iPortType == MV_FG_DATA_SOURCE_DEVICE)
    {
        return MV_FG_SetFloatValue(m_hDevice, strKey, fValue);
    }

    //return MV_FG_SetFloatValue(hPort, strKey, fValue);
}

// ch:获取和设置Bool型参数，如 ReverseX，详细内容参考SDK安装目录下的 MvCameraNode.xlsx 文件
// en:Get Bool type parameters, such as ReverseX, for details please refer to MvCameraNode.xlsx file under SDK installation directory
int CMvfgCamera::GetBoolValue(IN int iPortType, IN const char* strKey, OUT bool *pbValue)
{
    if (iPortType == MV_FG_DATA_SOURCE_INTERFACE)
    {
        return MV_FG_GetBoolValue(m_hInterface, strKey, pbValue);
    }
    else if (iPortType == MV_FG_DATA_SOURCE_DEVICE)
    {
        return MV_FG_GetBoolValue(m_hDevice, strKey, pbValue);
    }

    //return MV_FG_GetBoolValue(hPort, strKey, pbValue);
}

int CMvfgCamera::SetBoolValue(IN int iPortType, IN const char* strKey, IN bool bValue)
{
    if (iPortType == MV_FG_DATA_SOURCE_INTERFACE)
    {
        return MV_FG_SetBoolValue(m_hInterface, strKey, bValue);
    }
    else if (iPortType == MV_FG_DATA_SOURCE_DEVICE)
    {
        return MV_FG_SetBoolValue(m_hDevice, strKey, bValue);
    }

    //return MV_FG_SetBoolValue(hPort, strKey, bValue);
}

// ch:获取和设置String型参数，如 DeviceUserID，详细内容参考SDK安装目录下的 MvCameraNode.xlsx 文件UserSetSave
// en:Get String type parameters, such as DeviceUserID, for details please refer to MvCameraNode.xlsx file under SDK installation directory
int CMvfgCamera::GetStringValue(IN int iPortType, IN const char* strKey, MV_FG_STRINGVALUE* pStringValue)
{
    if (iPortType == MV_FG_DATA_SOURCE_INTERFACE)
    {
        return MV_FG_GetStringValue(m_hInterface, strKey, pStringValue);
    }
    else if (iPortType == MV_FG_DATA_SOURCE_DEVICE)
    {
        return MV_FG_GetStringValue(m_hDevice, strKey, pStringValue);
    }

    //return MV_FG_GetStringValue(hPort, strKey, pStringValue);
}

int CMvfgCamera::SetStringValue(IN int iPortType, IN const char* strKey, IN const char* strValue)
{
    if (iPortType == MV_FG_DATA_SOURCE_INTERFACE)
    {
        return MV_FG_SetStringValue(m_hInterface, strKey, strValue);
    }
    else if (iPortType == MV_FG_DATA_SOURCE_DEVICE)
    {
        return MV_FG_SetStringValue(m_hDevice, strKey, strValue);
    }

    //return MV_FG_SetStringValue(hPort, strKey, strValue);
}

// ch:执行一次Command型命令，如 UserSetSave，详细内容参考SDK安装目录下的 MvCameraNode.xlsx 文件
// en:Execute Command once, such as UserSetSave, for details please refer to MvCameraNode.xlsx file under SDK installation directory
int CMvfgCamera::CommandExecute(IN PORTHANDLE hPort, IN const char* strKey)
{
    return MV_FG_SetCommandValue(hPort, strKey);
}

// ch:探测网络最佳包大小(只对GigE相机有效) | en:Detection network optimal package size(It only works for the GigE camera)
//int CMvfgCamera::GetOptimalPacketSize(unsigned int* pOptimalPacketSize)
//{
//    if (MV_NULL == pOptimalPacketSize)
//    {
//        return MV_E_PARAMETER;
//    }
//
//    int nRet = MV_CC_GetOptimalPacketSize(m_hDevHandle);
//    if (nRet < MV_OK)
//    {
//        return nRet;
//    }
//
//    *pOptimalPacketSize = (unsigned int)nRet;
//
//    return MV_OK;
//}

// ch:注册消息异常回调 | en:Register Message Exception CallBack
int CMvfgCamera::RegisterExceptionCallBack(PORTHANDLE hPort, MV_FG_ExceptionCallBack cbException, void* pUser)
{
    return MV_FG_RegisterExceptionCallBack(hPort, cbException, pUser);
}

// ch:注册单个事件回调 | en:Register Event CallBack
int CMvfgCamera::RegisterEventCallBack(PORTHANDLE hPort, const char* pEventName, MV_FG_EventCallBack cbEvent, void* pUser)
{
    return MV_FG_RegisterEventCallBack(hPort, pEventName, cbEvent, pUser);
}

// ch:强制IP | en:Force IP
//int CMvfgCamera::ForceIp(unsigned int nIP, unsigned int nSubNetMask, unsigned int nDefaultGateWay)
//{
//    return MV_GIGE_ForceIpEx(m_hDevHandle, nIP, nSubNetMask, nDefaultGateWay);
//}

// ch:配置IP方式 | en:IP configuration method
//int CMvfgCamera::SetIpConfig(unsigned int nType)
//{
//    return MV_GIGE_SetIpConfig(m_hDevHandle, nType);
//}

// ch:强制IP | en:Force IP
//int CMvfgCamera::ForceIp(IN MV_CC_DEVICE_INFO* device, unsigned int nIP, unsigned int nSubNetMask, unsigned int nDefaultGateWay, std::wstring& sMsg)
//{
//    void* handle;
//    int nRet = MV_CC_CreateHandle(&handle, device);
//    if (MV_OK != nRet)
//    {
//        sMsg = L"Create handle fail";
//        return nRet;
//    }
//
//    // ch:判断设备IP是否可达
//    bool bAccessible = MV_CC_IsDeviceAccessible(device, MV_ACCESS_Exclusive);
//    if (bAccessible)
//    {
//        nRet = MV_GIGE_SetIpConfig(handle, MV_IP_CFG_STATIC);
//        if (MV_OK != nRet)
//        {
//            sMsg = L"Set Ip config fail";
//            MV_CC_DestroyHandle(handle);
//            return nRet;
//        }
//        nRet = MV_GIGE_ForceIpEx(handle, nIP, nSubNetMask, nDefaultGateWay);
//        if (MV_OK != nRet)
//        {
//            sMsg = L"ForceIp fail";
//            MV_CC_DestroyHandle(handle);
//            return nRet;
//        }
//    }
//    else
//    {
//        nRet = MV_GIGE_ForceIpEx(handle, nIP, nSubNetMask, nDefaultGateWay);
//        if (MV_OK != nRet)
//        {
//            sMsg = L"ForceIp fail";
//            MV_CC_DestroyHandle(handle);
//            return nRet;
//        }
//        MV_CC_DestroyHandle(handle);
//        handle = NULL;
//
//        //ch:需要重新创建句柄，设置为静态IP方式进行保存
//        device->SpecialInfo.stGigEInfo.nCurrentIp = nIP;
//        device->SpecialInfo.stGigEInfo.nCurrentSubNetMask = nSubNetMask;
//        device->SpecialInfo.stGigEInfo.nDefultGateWay = nDefaultGateWay;
//        int nRet = MV_CC_CreateHandle(&handle, device);
//        if (MV_OK != nRet)
//        {
//            sMsg = L"Create handle fail";
//            return nRet;
//        }
//
//        nRet = MV_GIGE_SetIpConfig(handle, MV_IP_CFG_STATIC);
//        if (MV_OK != nRet)
//        {
//            sMsg = L"Set Ip config fail";
//            MV_CC_DestroyHandle(handle);
//            return nRet;
//        }
//    }
//
//
//    MV_CC_DestroyHandle(handle);
//    return MV_OK;
//}

// ch:设置网络传输模式 | en:Set Net Transfer Mode
//int CMvfgCamera::SetNetTransMode(unsigned int nType)
//{
//    return MV_GIGE_SetNetTransMode(m_hDevHandle, nType);
//}

// ch:像素格式转换 | en:Pixel format conversion
int CMvfgCamera::ConvertPixelType(IMAGEHANDLE hImage, MV_FG_CONVERT_PIXEL_INFO* pstCvtParam)
{
    return MV_FG_ConvertPixelType(hImage, pstCvtParam);
}

// ch:保存图片 | en:save image
//int CMvfgCamera::SaveImage(MV_SAVE_IMAGE_PARAM_EX3* pstParam)
//{
//    return MV_CC_SaveImageEx3(m_hDevHandle, pstParam);
//}

// ch:保存图片为文件 | en:Save the image as a file
//int CMvfgCamera::SaveImageToFile(MV_SAVE_IMAGE_TO_FILE_PARAM_EX* pstSaveFileParam)
//{
//    return MV_CC_SaveImageToFileEx(m_hDevHandle, pstSaveFileParam);
//}

//保存BMP图像
int CMvfgCamera::SaveBitmap(MV_FG_SAVE_BITMAP_INFO* pstSaveBitmapInfo)
{
    return MV_FG_SaveBitmap(m_hStream, pstSaveBitmapInfo);
}
//保存JPEG图像
int CMvfgCamera::SaveJpeg(MV_FG_SAVE_JPEG_INFO* pstSaveJpegInfo)
{
    return MV_FG_SaveJpeg(m_hStream, pstSaveJpegInfo);
}
//保存TIFF图像
int CMvfgCamera::SaveTiff(MV_FG_SAVE_TIFF_TO_FILE_INFO* pstSaveTiffInfo)
{
    return MV_FG_SaveTiffToFile(m_hStream, pstSaveTiffInfo);
}
//保存BMP图像
int CMvfgCamera::SavePNG(MV_FG_SAVE_PNG_TO_FILE_INFO* pstSavePngInfo)
{
    return MV_FG_SavePngToFile(m_hStream, pstSavePngInfo);
}


// ch:绘制圆形辅助线 | en:Draw circle auxiliary line
int CMvfgCamera::DrawCircle(MVFG_CIRCLE_INFO* pCircleInfo)
{
    return MV_FG_DrawCircle(m_hDevice, pCircleInfo);
}

// ch:绘制线形辅助线 | en:Draw lines auxiliary line
int CMvfgCamera::DrawLines(MVFG_LINES_INFO* pLinesInfo)
{
    return MV_FG_DrawLines(m_hDevice, pLinesInfo);
}

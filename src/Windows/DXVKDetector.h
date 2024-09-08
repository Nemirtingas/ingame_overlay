/*
 * Copyright (C) Nemirtingas
 * This file is part of the ingame overlay project
 *
 * The ingame overlay project is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * The ingame overlay project is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the ingame overlay project; if not, see
 * <http://www.gnu.org/licenses/>.
 */

namespace InGameOverlay {

// DX9
constexpr static GUID ID3D9VkInteropInterfaceGUID{ 0x3461a81b, 0xce41, 0x485b, { 0xb6, 0xb5, 0xfc, 0xf0, 0x8b, 0xa6, 0xa6, 0xbd } };
constexpr static GUID ID3D9VkInteropTextureGUID  { 0xd56344f5, 0x8d35, 0x46fd, { 0x80, 0x6d, 0x94, 0xc3, 0x51, 0xb4, 0x72, 0xc1 } };
constexpr static GUID ID3D9VkInteropDeviceGUID   { 0x2eaa4b89, 0x0107, 0x4bdb, { 0x87, 0xf7, 0x0f, 0x54, 0x1c, 0x49, 0x3c, 0xe0 } };
constexpr static GUID ID3D9VkExtSwapchainGUID    { 0x13776e93, 0x4aa9, 0x430a, { 0xa4, 0xec, 0xfe, 0x9e, 0x28, 0x11, 0x81, 0xd5 } };

/**
 * \brief D3D9 interface for Vulkan interop
 *
 * Provides access to the instance and physical device
 * handles for the given D3D9 interface and adapter ordinals.
 */
struct ID3D9VkInteropInterface : public IUnknown{
    /**
     * \brief Queries Vulkan handles used by DXVK
     *
     * \param [out] pInstance The Vulkan instance
     */
    virtual void STDMETHODCALLTYPE GetInstanceHandle(
            VkInstance * pInstance) = 0;

    /**
     * \brief Queries Vulkan handles used by DXVK
     *
     * \param [in] Adapter Adapter ordinal
     * \param [out] pInstance The Vulkan instance
     */
    virtual void STDMETHODCALLTYPE GetPhysicalDeviceHandle(
            UINT                  Adapter,
            VkPhysicalDevice* pPhysicalDevice) = 0;
};

/**
 * \brief D3D9 texture interface for Vulkan interop
 *
 * Provides access to the backing resource of a
 * D3D9 texture.
 */
struct ID3D9VkInteropTexture : public IUnknown{
    /**
     * \brief Retrieves Vulkan image info
     *
     * Retrieves both the image handle as well as the image's
     * properties. Any of the given pointers may be \c nullptr.
     *
     * If \c pInfo is not \c nullptr, the following rules apply:
     * - \c pInfo->sType \e must be \c VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
     * - \c pInfo->pNext \e must be \c nullptr or point to a supported
     *   extension-specific structure (currently none)
     * - \c pInfo->queueFamilyIndexCount must be the length of the
     *   \c pInfo->pQueueFamilyIndices array, in \c uint32_t units.
     * - \c pInfo->pQueueFamilyIndices must point to a pre-allocated
     *   array of \c uint32_t of size \c pInfo->pQueueFamilyIndices.
     *
     * \note As of now, the sharing mode will always be
     *       \c VK_SHARING_MODE_EXCLUSIVE and no queue
     *       family indices will be written to the array.
     *
     * After the call, the structure pointed to by \c pInfo can
     * be used to create an image with identical properties.
     *
     * If \c pLayout is not \c nullptr, it will receive the
     * layout that the image will be in after flushing any
     * outstanding commands on the device.
     * \param [out] pHandle The image handle
     * \param [out] pLayout Image layout
     * \param [out] pInfo Image properties
     * \returns \c S_OK on success, or \c D3DERR_INVALIDCALL
     */
    virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(
            VkImage * pHandle,
            VkImageLayout * pLayout,
            VkImageCreateInfo * pInfo) = 0;
};

/**
 * \brief D3D9 device interface for Vulkan interop
 *
 * Provides access to the device and instance handles
 * as well as the queue that is used for rendering.
 */
struct ID3D9VkInteropDevice : public IUnknown{
    /**
     * \brief Queries Vulkan handles used by DXVK
     *
     * \param [out] pInstance The Vulkan instance
     * \param [out] pPhysDev The physical device
     * \param [out] pDevide The device handle
     */
    virtual void STDMETHODCALLTYPE GetVulkanHandles(
            VkInstance * pInstance,
            VkPhysicalDevice * pPhysDev,
            VkDevice * pDevice) = 0;

    /**
     * \brief Queries the rendering queue used by DXVK
     *
     * \param [out] pQueue The Vulkan queue handle
     * \param [out] pQueueIndex Queue index
     * \param [out] pQueueFamilyIndex Queue family index
     */
    virtual void STDMETHODCALLTYPE GetSubmissionQueue(
            VkQueue* pQueue,
            uint32_t* pQueueIndex,
            uint32_t* pQueueFamilyIndex) = 0;

    /**
     * \brief Transitions a Texture to a given layout
     *
     * Executes an explicit image layout transition on the
     * D3D device. Note that the image subresources \e must
     * be transitioned back to its original layout before
     * using it again from D3D9.
     * Synchronization is left up to the caller.
     * This function merely emits a call to transition the
     * texture on the DXVK internal command stream.
     * \param [in] pTexture The image to transform
     * \param [in] pSubresources Subresources to transform
     * \param [in] OldLayout Current image layout
     * \param [in] NewLayout Desired image layout
     */
    virtual void STDMETHODCALLTYPE TransitionTextureLayout(
            ID3D9VkInteropTexture* pTexture,
      const VkImageSubresourceRange* pSubresources,
            VkImageLayout             OldLayout,
            VkImageLayout             NewLayout) = 0;

    /**
     * \brief Flushes outstanding D3D rendering commands
     *
     * Must be called before submitting Vulkan commands
     * to the rendering queue if those commands use the
     * backing resource of a D3D9 object.
     */
    virtual void STDMETHODCALLTYPE FlushRenderingCommands() = 0;

    /**
     * \brief Locks submission queue
     *
     * Should be called immediately before submitting
     * Vulkan commands to the rendering queue in order
     * to prevent DXVK from using the queue.
     *
     * While the submission queue is locked, no D3D9
     * methods must be called from the locking thread,
     * or otherwise a deadlock might occur.
     */
    virtual void STDMETHODCALLTYPE LockSubmissionQueue() = 0;

    /**
     * \brief Releases submission queue
     *
     * Should be called immediately after submitting
     * Vulkan commands to the rendering queue in order
     * to allow DXVK to submit new commands.
     */
    virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue() = 0;

    /**
     * \brief Locks the device
     *
     * Can be called to ensure no D3D9 device methods
     * can be executed until UnlockDevice has been called.
     *
     * This will do nothing if the D3DCREATE_MULTITHREADED
     * is not set.
     */
    virtual void STDMETHODCALLTYPE LockDevice() = 0;

    /**
     * \brief Unlocks the device
     *
     * Must only be called after a call to LockDevice.
     */
    virtual void STDMETHODCALLTYPE UnlockDevice() = 0;

    /**
     * \brief Wait for a resource to finish being used
     *
     * Waits for the GPU resource associated with the
     * resource to finish being used by the GPU.
     *
     * Valid D3DLOCK flags:
     *  - D3DLOCK_READONLY:  Only waits for writes
     *  - D3DLOCK_DONOTWAIT: Does not wait for the resource (may flush)
     *
     * \param [in] pResource Resource to be waited upon
     * \param [in] MapFlags D3DLOCK flags
     * \returns true if the resource is ready to use,
     *          false if the resource is till in use
     */
    virtual bool STDMETHODCALLTYPE WaitForResource(
            IDirect3DResource9* pResource,
            DWORD                MapFlags) = 0;
};

/**
 * \brief D3D9 current output metadata
 */
struct D3D9VkExtOutputMetadata {
    float RedPrimary[2];
    float GreenPrimary[2];
    float BluePrimary[2];
    float WhitePoint[2];
    float MinLuminance;
    float MaxLuminance;
    float MaxFullFrameLuminance;
};

/**
 * \brief D3D9 extended swapchain
 */
struct ID3D9VkExtSwapchain : public IUnknown{
  virtual BOOL STDMETHODCALLTYPE CheckColorSpaceSupport(
          VkColorSpaceKHR           ColorSpace) = 0;

  virtual HRESULT STDMETHODCALLTYPE SetColorSpace(
          VkColorSpaceKHR           ColorSpace) = 0;

  virtual HRESULT STDMETHODCALLTYPE SetHDRMetaData(
    const VkHdrMetadataEXT* pHDRMetadata) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetCurrentOutputDesc(
          D3D9VkExtOutputMetadata* pOutputDesc) = 0;

  virtual void STDMETHODCALLTYPE UnlockAdditionalFormats() = 0;
};

// DXGI
constexpr static GUID IDXGIDXVKAdapterGUID       { 0x907bf281, 0xea3c, 0x43b4, { 0xa8, 0xe4, 0x9f, 0x23, 0x11, 0x07, 0xb4, 0xff } };
constexpr static GUID IDXGIDXVKDeviceGUID        { 0x92a5d77b, 0xb6e1, 0x420a, { 0xb2, 0x60, 0xfd, 0xf7, 0x01, 0x27, 0x28, 0x27 } };
constexpr static GUID IDXGIVkMonitorInfoGUID     { 0xc06a236f, 0x5be3, 0x448a, { 0x89, 0x43, 0x89, 0xc6, 0x11, 0xc0, 0xc2, 0xc1 } };
constexpr static GUID IDXGIVkInteropFactoryGUID  { 0x4c5e1b0d, 0xb0c8, 0x4131, { 0xbf, 0xd8, 0x9b, 0x24, 0x76, 0xf7, 0xf4, 0x08 } };
constexpr static GUID IDXGIVkInteropFactory1GUID { 0x2a289dbd, 0x2d0a, 0x4a51, { 0x89, 0xf7, 0xf2, 0xad, 0xce, 0x46, 0x5c, 0xd6 } };
constexpr static GUID IDXGIVkInteropAdapterGUID  { 0x3a6d8f2c, 0xb0e8, 0x4ab4, { 0xb4, 0xdc, 0x4f, 0xd2, 0x48, 0x91, 0xbf, 0xa5 } };
constexpr static GUID IDXGIVkInteropDeviceGUID   { 0xe2ef5fa5, 0xdc21, 0x4af7, { 0x90, 0xc4, 0xf6, 0x7e, 0xf6, 0xa0, 0x93, 0x23 } };
constexpr static GUID IDXGIVkInteropDevice1GUID  { 0xe2ef5fa5, 0xdc21, 0x4af7, { 0x90, 0xc4, 0xf6, 0x7e, 0xf6, 0xa0, 0x93, 0x24 } };
constexpr static GUID IDXGIVkInteropSurfaceGUID  { 0x5546cf8c, 0x77e7, 0x4341, { 0xb0, 0x5d, 0x8d, 0x4d, 0x50, 0x00, 0xe7, 0x7d } };
constexpr static GUID IDXGIVkSurfaceFactoryGUID  { 0x1e7895a1, 0x1bc3, 0x4f9c, { 0xa6, 0x70, 0x29, 0x0a, 0x4b, 0xc9, 0x58, 0x1a } };
constexpr static GUID IDXGIVkSwapChainGUID       { 0xe4a9059e, 0xb569, 0x46ab, { 0x8d, 0xe7, 0x50, 0x1b, 0xd2, 0xbc, 0x7f, 0x7a } };
constexpr static GUID IDXGIVkSwapChain1GUID      { 0x785326d4, 0xb77b, 0x4826, { 0xae, 0x70, 0x8d, 0x08, 0x30, 0x8e, 0xe6, 0xd1 } };
constexpr static GUID IDXGIVkSwapChain2GUID      { 0xaed91093, 0xe02e, 0x458c, { 0xbd, 0xef, 0xa9, 0x7d, 0xa1, 0xa7, 0xe6, 0xd2 } };
constexpr static GUID IDXGIVkSwapChainFactoryGUID{ 0xe7d6c3ca, 0x23a0, 0x4e08, { 0x9f, 0x2f, 0xea, 0x52, 0x31, 0xdf, 0x66, 0x33 } };

/**
 * \brief Private DXGI device interface
 */
struct IDXGIDXVKDevice : public IUnknown{
  virtual void STDMETHODCALLTYPE SetAPIVersion(
            UINT                    Version) = 0;

  virtual UINT STDMETHODCALLTYPE GetAPIVersion() = 0;

};

struct IDXGIVkInteropDevice;

/**
 * \brief DXGI surface interface for Vulkan interop
 *
 * Provides access to the backing resource of a
 * DXGI surface, which is typically a D3D texture.
 */
struct IDXGIVkInteropSurface : public IUnknown{
    /**
     * \brief Retrieves device interop interfaceSlots
     *
     * Queries the device that owns the surface for
     * the \ref IDXGIVkInteropDevice interface.
     * \param [out] ppDevice The device interface
     * \returns \c S_OK on success
     */
    virtual HRESULT STDMETHODCALLTYPE GetDevice(
            IDXGIVkInteropDevice **ppDevice) = 0;

    /**
     * \brief Retrieves Vulkan image info
     *
     * Retrieves both the image handle as well as the image's
     * properties. Any of the given pointers may be \c nullptr.
     *
     * If \c pInfo is not \c nullptr, the following rules apply:
     * - \c pInfo->sType \e must be \c VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
     * - \c pInfo->pNext \e must be \c nullptr or point to a supported
     *   extension-specific structure (currently none)
     * - \c pInfo->queueFamilyIndexCount must be the length of the
     *   \c pInfo->pQueueFamilyIndices array, in \c uint32_t units.
     * - \c pInfo->pQueueFamilyIndices must point to a pre-allocated
     *   array of \c uint32_t of size \c pInfo->pQueueFamilyIndices.
     *
     * \note As of now, the sharing mode will always be
     *       \c VK_SHARING_MODE_EXCLUSIVE and no queue
     *       family indices will be written to the array.
     *
     * After the call, the structure pointed to by \c pInfo can
     * be used to create an image with identical properties.
     *
     * If \c pLayout is not \c nullptr, it will receive the
     * layout that the image will be in after flushing any
     * outstanding commands on the device.
     * \param [out] pHandle The image handle
     * \param [out] pLayout Image layout
     * \param [out] pInfo Image properties
     * \returns \c S_OK on success, or \c E_INVALIDARG
     */
    virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(
            VkImage* pHandle,
            VkImageLayout* pLayout,
            VkImageCreateInfo* pInfo) = 0;
};

/**
 * \brief DXGI device interface for Vulkan interop
 *
 * Provides access to the device and instance handles
 * as well as the queue that is used for rendering.
 */
struct IDXGIVkInteropDevice : public IUnknown{
    /**
     * \brief Queries Vulkan handles used by DXVK
     *
     * \param [out] pInstance The Vulkan instance
     * \param [out] pPhysDev The physical device
     * \param [out] pDevide The device handle
     */
    virtual void STDMETHODCALLTYPE GetVulkanHandles(
            VkInstance * pInstance,
            VkPhysicalDevice * pPhysDev,
            VkDevice * pDevice) = 0;

    /**
     * \brief Queries the rendering queue used by DXVK
     *
     * \param [out] pQueue The Vulkan queue handle
     * \param [out] pQueueFamilyIndex Queue family index
     */
    virtual void STDMETHODCALLTYPE GetSubmissionQueue(
            VkQueue* pQueue,
            uint32_t* pQueueFamilyIndex) = 0;

    /**
     * \brief Transitions a surface to a given layout
     *
     * Executes an explicit image layout transition on the
     * D3D device. Note that the image subresources \e must
     * be transitioned back to its original layout before
     * using it again from D3D11.
     * \param [in] pSurface The image to transform
     * \param [in] pSubresources Subresources to transform
     * \param [in] OldLayout Current image layout
     * \param [in] NewLayout Desired image layout
     */
    virtual void STDMETHODCALLTYPE TransitionSurfaceLayout(
            IDXGIVkInteropSurface* pSurface,
      const VkImageSubresourceRange* pSubresources,
            VkImageLayout             OldLayout,
            VkImageLayout             NewLayout) = 0;

    /**
     * \brief Flushes outstanding D3D rendering commands
     *
     * Must be called before submitting Vulkan commands
     * to the rendering queue if those commands use the
     * backing resource of a D3D11 object.
     */
    virtual void STDMETHODCALLTYPE FlushRenderingCommands() = 0;

    /**
     * \brief Locks submission queue
     *
     * Should be called immediately before submitting
     * Vulkan commands to the rendering queue in order
     * to prevent DXVK from using the queue.
     *
     * While the submission queue is locked, no D3D11
     * methods must be called from the locking thread,
     * or otherwise a deadlock might occur.
     */
    virtual void STDMETHODCALLTYPE LockSubmissionQueue() = 0;

    /**
     * \brief Releases submission queue
     *
     * Should be called immediately after submitting
     * Vulkan commands to the rendering queue in order
     * to allow DXVK to submit new commands.
     */
    virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue() = 0;
};

struct D3D11_TEXTURE2D_DESC1;
struct ID3D11Texture2D;

/**
 * \brief See IDXGIVkInteropDevice.
 */
struct IDXGIVkInteropDevice1 : public IDXGIVkInteropDevice{
    /**
     * \brief Queries the rendering queue used by DXVK
     *
     * \param [out] pQueue The Vulkan queue handle
     * \param [out] pQueueIndex Queue index
     * \param [out] pQueueFamilyIndex Queue family index
     */
    virtual void STDMETHODCALLTYPE GetSubmissionQueue1(
            VkQueue * pQueue,
            uint32_t * pQueueIndex,
            uint32_t * pQueueFamilyIndex) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateTexture2DFromVkImage(
      const D3D11_TEXTURE2D_DESC1* pDesc,
            VkImage               vkImage,
            ID3D11Texture2D** ppTexture2D) = 0;
};

}
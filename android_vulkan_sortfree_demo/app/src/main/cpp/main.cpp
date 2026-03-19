#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef HAS_SHADERC
#include <shaderc/shaderc.h>
#endif

#include "ply_loader.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SortFreeDemo", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SortFreeDemo", __VA_ARGS__)

static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

struct Mat4 {
    float m[16]{};
};

static Mat4 Identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
    return r;
}

static Mat4 Mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float v = 0.f;
            for (int k = 0; k < 4; ++k) v += a.m[i*4+k] * b.m[k*4+j];
            r.m[i*4+j] = v;
        }
    }
    return r;
}

static Mat4 Perspective(float fovyRad, float aspect, float zNear, float zFar) {
    Mat4 r{};
    float f = 1.f / std::tan(fovyRad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = zFar / (zNear - zFar);
    r.m[11] = -1.f;
    r.m[14] = (zNear * zFar) / (zNear - zFar);
    return r;
}

static Mat4 Translate(float x, float y, float z) {
    Mat4 r = Identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

static Mat4 RotateY(float a) {
    Mat4 r = Identity();
    r.m[0] = std::cos(a); r.m[2] = std::sin(a);
    r.m[8] = -std::sin(a); r.m[10] = std::cos(a);
    return r;
}

static Mat4 RotateX(float a) {
    Mat4 r = Identity();
    r.m[5] = std::cos(a); r.m[6] = -std::sin(a);
    r.m[9] = std::sin(a); r.m[10] = std::cos(a);
    return r;
}

struct Vertex {
    float x,y,z,scale;
    float r,g,b,opacity;
};

struct TouchState {
    bool down = false;
    float lastX = 0.f;
    float lastY = 0.f;
    float yaw = 0.f;
    float pitch = 0.f;
    float dist = 2.5f;
};

struct VulkanContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmdBuffers;

    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailable{};
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> renderFinished{};
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> inFlight{};
    uint32_t frameIndex = 0;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMem = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;

#ifdef HAS_SHADERC
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
#endif

    bool initialized = false;
};

static bool VkCheck(VkResult r, const char* where) {
    if (r != VK_SUCCESS) {
        LOGE("Vulkan error %d at %s", r, where);
        return false;
    }
    return true;
}

static std::string LoadAssetText(AAssetManager* mgr, const char* name) {
    AAsset* asset = AAssetManager_open(mgr, name, AASSET_MODE_BUFFER);
    if (!asset) return {};
    size_t len = AAsset_getLength(asset);
    std::string s(len, '\0');
    int64_t rd = AAsset_read(asset, s.data(), len);
    AAsset_close(asset);
    if (rd <= 0) return {};
    return s;
}

static bool LoadPlyCloudFromAssets(android_app* app, PlyCloud& out) {
    std::string text = LoadAssetText(app->activity->assetManager, "scene.ply");
    if (text.empty()) {
        LOGE("assets/scene.ply not found");
        return false;
    }
    std::string err;
    if (!LoadAsciiPlyFromMemory(text.data(), text.size(), out, err)) {
        LOGE("PLY parse failed: %s", err.c_str());
        return false;
    }
    LOGI("PLY loaded: %zu points", out.points.size());
    return true;
}

static uint32_t FindMemoryType(VkPhysicalDevice pd, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i=0; i<mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u<<i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    return UINT32_MAX;
}

static bool PickPhysicalDevice(VulkanContext& vk) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(vk.instance, &count, nullptr);
    if (!count) return false;
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(vk.instance, &count, devs.data());

    for (auto d : devs) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, qProps.data());
        for (uint32_t i = 0; i < qCount; ++i) {
            VkBool32 support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, vk.surface, &support);
            if (support && (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                vk.physicalDevice = d;
                vk.queueFamily = i;
                return true;
            }
        }
    }
    return false;
}

static bool CreateSwapchain(VulkanContext& vk, ANativeWindow* window) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physicalDevice, vk.surface, &caps);

    uint32_t fcount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physicalDevice, vk.surface, &fcount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fcount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physicalDevice, vk.surface, &fcount, formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = f; break; }
    }

    vk.swapchainFormat = chosen.format;
    vk.extent = caps.currentExtent;
    if (vk.extent.width == 0xFFFFFFFFu) {
        vk.extent.width = static_cast<uint32_t>(ANativeWindow_getWidth(window));
        vk.extent.height = static_cast<uint32_t>(ANativeWindow_getHeight(window));
    }

    uint32_t imageCount = std::max(caps.minImageCount + 1, 2u);
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = vk.surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = vk.swapchainFormat;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = vk.extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;

    if (!VkCheck(vkCreateSwapchainKHR(vk.device, &ci, nullptr, &vk.swapchain), "vkCreateSwapchainKHR")) return false;

    uint32_t ic = 0;
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &ic, nullptr);
    vk.images.resize(ic);
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &ic, vk.images.data());

    vk.views.resize(ic);
    for (size_t i = 0; i < vk.images.size(); ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = vk.images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = vk.swapchainFormat;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (!VkCheck(vkCreateImageView(vk.device, &vi, nullptr, &vk.views[i]), "vkCreateImageView")) return false;
    }
    return true;
}

static bool CreateRenderPassAndFramebuffers(VulkanContext& vk) {
    VkAttachmentDescription color{};
    color.format = vk.swapchainFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    if (!VkCheck(vkCreateRenderPass(vk.device, &rp, nullptr, &vk.renderPass), "vkCreateRenderPass")) return false;

    vk.framebuffers.resize(vk.views.size());
    for (size_t i = 0; i < vk.views.size(); ++i) {
        VkImageView attachments[] = {vk.views[i]};
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = vk.renderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = attachments;
        fb.width = vk.extent.width;
        fb.height = vk.extent.height;
        fb.layers = 1;
        if (!VkCheck(vkCreateFramebuffer(vk.device, &fb, nullptr, &vk.framebuffers[i]), "vkCreateFramebuffer")) return false;
    }
    return true;
}

#ifdef HAS_SHADERC
static std::vector<uint32_t> CompileGLSL(const std::string& src, shaderc_shader_kind kind, const char* name) {
    shaderc_compiler_t c = shaderc_compiler_initialize();
    shaderc_compile_options_t opt = shaderc_compile_options_initialize();
    shaderc_compilation_result_t r = shaderc_compile_into_spv(c, src.c_str(), src.size(), kind, name, "main", opt);
    std::vector<uint32_t> out;
    if (shaderc_result_get_compilation_status(r) != shaderc_compilation_status_success) {
        LOGE("Shader compile failed (%s): %s", name, shaderc_result_get_error_message(r));
    } else {
        auto ptr = reinterpret_cast<const uint32_t*>(shaderc_result_get_bytes(r));
        size_t n = shaderc_result_get_length(r) / sizeof(uint32_t);
        out.assign(ptr, ptr + n);
    }
    shaderc_result_release(r);
    shaderc_compile_options_release(opt);
    shaderc_compiler_release(c);
    return out;
}

static VkShaderModule CreateShader(VkDevice d, const std::vector<uint32_t>& spv) {
    if (spv.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = spv.size() * sizeof(uint32_t);
    ci.pCode = spv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(d, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}
#endif

static bool CreateVertexBuffer(VulkanContext& vk, const PlyCloud& cloud) {
    std::vector<Vertex> verts;
    verts.reserve(cloud.points.size());
    for (auto& p : cloud.points) {
        verts.push_back({p.x, p.y, p.z, p.scale, p.r, p.g, p.b, p.opacity});
    }
    vk.vertexCount = static_cast<uint32_t>(verts.size());
    if (!vk.vertexCount) return true;

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = sizeof(Vertex) * verts.size();
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!VkCheck(vkCreateBuffer(vk.device, &bi, nullptr, &vk.vertexBuffer), "vkCreateBuffer")) return false;

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(vk.device, vk.vertexBuffer, &mr);

    uint32_t mt = FindMemoryType(vk.physicalDevice, mr.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) return false;

    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mt;
    if (!VkCheck(vkAllocateMemory(vk.device, &ai, nullptr, &vk.vertexMem), "vkAllocateMemory")) return false;
    VkCheck(vkBindBufferMemory(vk.device, vk.vertexBuffer, vk.vertexMem, 0), "vkBindBufferMemory");

    void* ptr = nullptr;
    vkMapMemory(vk.device, vk.vertexMem, 0, bi.size, 0, &ptr);
    std::memcpy(ptr, verts.data(), (size_t)bi.size);
    vkUnmapMemory(vk.device, vk.vertexMem);
    return true;
}

#ifdef HAS_SHADERC
static bool CreatePointPipeline(VulkanContext& vk) {
    const char* vsrc = R"(
#version 450
layout(location=0) in vec4 inPosScale;
layout(location=1) in vec4 inColorOpacity;
layout(push_constant) uniform Push { mat4 mvp; } pc;
layout(location=0) out vec4 vColorOpacity;
layout(location=1) out float vDepth;
void main() {
    vec4 p = pc.mvp * vec4(inPosScale.xyz, 1.0);
    gl_Position = p;
    gl_PointSize = max(1.0, 600.0 * inPosScale.w / max(1e-4, p.w));
    vColorOpacity = inColorOpacity;
    vDepth = max(1e-4, p.w);
}
)";
    const char* fsrc = R"(
#version 450
layout(location=0) in vec4 vColorOpacity;
layout(location=1) in float vDepth;
layout(location=0) out vec4 outColor;
void main(){
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float power = -dot(uv, uv) * 2.0;
    float alpha = clamp(vColorOpacity.a * exp(power), 0.0, 0.99);
    if (alpha < 1.0/255.0) discard;
    float phi = 1.0;
    float weight = exp(0.05 / vDepth) + phi / (vDepth * vDepth) + phi * phi;
    vec3 c = vColorOpacity.rgb * alpha * weight;
    outColor = vec4(c, alpha);
}
)";

    auto vspv = CompileGLSL(vsrc, shaderc_glsl_vertex_shader, "gaussian.vert");
    auto fspv = CompileGLSL(fsrc, shaderc_glsl_fragment_shader, "gaussian.frag");
    VkShaderModule vs = CreateShader(vk.device, vspv);
    VkShaderModule fs = CreateShader(vk.device, fspv);
    if (!vs || !fs) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bind{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16};

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    VkViewport vp{0,0,(float)vk.extent.width,(float)vk.extent.height,0,1};
    VkRect2D sc{{0,0}, vk.extent};
    VkPipelineViewportStateCreateInfo vsi{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vsi.viewportCount = 1; vsi.pViewports = &vp;
    vsi.scissorCount = 1; vsi.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = 0xF;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    if (!VkCheck(vkCreatePipelineLayout(vk.device, &pl, nullptr, &vk.pipelineLayout), "vkCreatePipelineLayout")) return false;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vsi;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.layout = vk.pipelineLayout;
    gp.renderPass = vk.renderPass;
    gp.subpass = 0;

    bool ok = VkCheck(vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &gp, nullptr, &vk.pipeline), "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(vk.device, vs, nullptr);
    vkDestroyShaderModule(vk.device, fs, nullptr);
    return ok;
}
#endif

static bool CreateCommandsAndSync(VulkanContext& vk) {
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.queueFamilyIndex = vk.queueFamily;
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (!VkCheck(vkCreateCommandPool(vk.device, &cp, nullptr, &vk.cmdPool), "vkCreateCommandPool")) return false;

    vk.cmdBuffers.resize(vk.framebuffers.size());
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = vk.cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = static_cast<uint32_t>(vk.cmdBuffers.size());
    if (!VkCheck(vkAllocateCommandBuffers(vk.device, &ai, vk.cmdBuffers.data()), "vkAllocateCommandBuffers")) return false;

    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (!VkCheck(vkCreateSemaphore(vk.device, &si, nullptr, &vk.imageAvailable[i]), "vkCreateSemaphore IA")) return false;
        if (!VkCheck(vkCreateSemaphore(vk.device, &si, nullptr, &vk.renderFinished[i]), "vkCreateSemaphore RF")) return false;
        if (!VkCheck(vkCreateFence(vk.device, &fi, nullptr, &vk.inFlight[i]), "vkCreateFence")) return false;
    }
    return true;
}

static bool InitVulkan(VulkanContext& vk, android_app* app, const PlyCloud& cloud) {
    const char* instExts[] = {"VK_KHR_surface", "VK_KHR_android_surface"};

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "SortFree Vulkan Demo";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &appInfo;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = instExts;
    if (!VkCheck(vkCreateInstance(&ici, nullptr, &vk.instance), "vkCreateInstance")) return false;

    VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    sci.window = app->window;
    if (!VkCheck(vkCreateAndroidSurfaceKHR(vk.instance, &sci, nullptr, &vk.surface), "vkCreateAndroidSurfaceKHR")) return false;

    if (!PickPhysicalDevice(vk)) {
        LOGE("No suitable physical device");
        return false;
    }

    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = vk.queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    if (!VkCheck(vkCreateDevice(vk.physicalDevice, &dci, nullptr, &vk.device), "vkCreateDevice")) return false;

    vkGetDeviceQueue(vk.device, vk.queueFamily, 0, &vk.queue);

    if (!CreateSwapchain(vk, app->window)) return false;
    if (!CreateRenderPassAndFramebuffers(vk)) return false;
    if (!CreateCommandsAndSync(vk)) return false;
    if (!CreateVertexBuffer(vk, cloud)) return false;
#ifdef HAS_SHADERC
    if (!CreatePointPipeline(vk)) {
        LOGE("Pipeline creation failed; fallback to clear-only mode.");
    }
#else
    LOGI("No shaderc found, fallback to clear-only mode.");
#endif

    vk.initialized = true;
    LOGI("Vulkan initialized: %ux%u", vk.extent.width, vk.extent.height);
    return true;
}

static void RecordCmd(VulkanContext& vk, VkCommandBuffer cb, uint32_t imageIndex, const TouchState& touch, size_t pointCount) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cb, &bi);

    float c0 = 0.02f + 0.3f * (0.5f + 0.5f * std::sin(touch.yaw));
    float c1 = 0.02f + 0.3f * (0.5f + 0.5f * std::sin(touch.pitch));
    float c2 = 0.02f + 0.2f * std::min(1.0f, float(pointCount % 100000) / 100000.f);

    VkClearValue clear{};
    clear.color = {{c0, c1, c2, 1.0f}};

    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = vk.renderPass;
    rp.framebuffer = vk.framebuffers[imageIndex];
    rp.renderArea.extent = vk.extent;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;

    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
#ifdef HAS_SHADERC
    if (vk.pipeline != VK_NULL_HANDLE && vk.vertexCount > 0) {
        Mat4 proj = Perspective(60.0f * 3.1415926f / 180.f, float(vk.extent.width) / float(vk.extent.height), 0.01f, 100.f);
        Mat4 view = Mul(Translate(0,0,-touch.dist), Mul(RotateX(touch.pitch), RotateY(touch.yaw)));
        Mat4 mvp = Mul(view, proj);

        VkDeviceSize off = 0;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &vk.vertexBuffer, &off);
        vkCmdPushConstants(cb, vk.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), &mvp);
        vkCmdDraw(cb, vk.vertexCount, 1, 0, 0);
    }
#endif
    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);
}

static void DrawFrame(VulkanContext& vk, const TouchState& touch, size_t pointCount) {
    const uint32_t fi = vk.frameIndex % MAX_FRAMES_IN_FLIGHT;
    vkWaitForFences(vk.device, 1, &vk.inFlight[fi], VK_TRUE, UINT64_MAX);
    vkResetFences(vk.device, 1, &vk.inFlight[fi]);

    uint32_t imageIndex = 0;
    VkResult acq = vkAcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX, vk.imageAvailable[fi], VK_NULL_HANDLE, &imageIndex);
    if (acq != VK_SUCCESS) return;

    vkResetCommandBuffer(vk.cmdBuffers[imageIndex], 0);
    RecordCmd(vk, vk.cmdBuffers[imageIndex], imageIndex, touch, pointCount);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &vk.imageAvailable[fi];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &vk.cmdBuffers[imageIndex];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &vk.renderFinished[fi];
    if (vkQueueSubmit(vk.queue, 1, &si, vk.inFlight[fi]) != VK_SUCCESS) return;

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &vk.renderFinished[fi];
    pi.swapchainCount = 1;
    pi.pSwapchains = &vk.swapchain;
    pi.pImageIndices = &imageIndex;
    vkQueuePresentKHR(vk.queue, &pi);

    vk.frameIndex++;
}

static void CleanupVulkan(VulkanContext& vk) {
    if (!vk.device) return;
    vkDeviceWaitIdle(vk.device);

#ifdef HAS_SHADERC
    if (vk.pipeline) vkDestroyPipeline(vk.device, vk.pipeline, nullptr);
    if (vk.pipelineLayout) vkDestroyPipelineLayout(vk.device, vk.pipelineLayout, nullptr);
#endif

    if (vk.vertexBuffer) vkDestroyBuffer(vk.device, vk.vertexBuffer, nullptr);
    if (vk.vertexMem) vkFreeMemory(vk.device, vk.vertexMem, nullptr);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vk.imageAvailable[i]) vkDestroySemaphore(vk.device, vk.imageAvailable[i], nullptr);
        if (vk.renderFinished[i]) vkDestroySemaphore(vk.device, vk.renderFinished[i], nullptr);
        if (vk.inFlight[i]) vkDestroyFence(vk.device, vk.inFlight[i], nullptr);
    }
    if (vk.cmdPool) vkDestroyCommandPool(vk.device, vk.cmdPool, nullptr);
    for (auto fb : vk.framebuffers) vkDestroyFramebuffer(vk.device, fb, nullptr);
    if (vk.renderPass) vkDestroyRenderPass(vk.device, vk.renderPass, nullptr);
    for (auto v : vk.views) vkDestroyImageView(vk.device, v, nullptr);
    if (vk.swapchain) vkDestroySwapchainKHR(vk.device, vk.swapchain, nullptr);
    vkDestroyDevice(vk.device, nullptr);
    if (vk.surface) vkDestroySurfaceKHR(vk.instance, vk.surface, nullptr);
    if (vk.instance) vkDestroyInstance(vk.instance, nullptr);

    vk = {};
}

static int32_t HandleInput(android_app* app, AInputEvent* event) {
    auto* touch = reinterpret_cast<TouchState*>(app->userData);
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
    const int act = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    const float x = AMotionEvent_getX(event, 0);
    const float y = AMotionEvent_getY(event, 0);

    if (act == AMOTION_EVENT_ACTION_DOWN) {
        touch->down = true;
        touch->lastX = x;
        touch->lastY = y;
        return 1;
    }
    if (act == AMOTION_EVENT_ACTION_UP || act == AMOTION_EVENT_ACTION_CANCEL) {
        touch->down = false;
        return 1;
    }
    if (act == AMOTION_EVENT_ACTION_MOVE && touch->down) {
        const float dx = x - touch->lastX;
        const float dy = y - touch->lastY;
        touch->yaw += dx * 0.01f;
        touch->pitch += dy * 0.01f;
        touch->pitch = std::clamp(touch->pitch, -1.4f, 1.4f);
        touch->lastX = x;
        touch->lastY = y;
        return 1;
    }
    return 0;
}

static void HandleCmd(android_app* app, int32_t cmd) {
    (void)app; (void)cmd;
}

void android_main(android_app* app) {
    app_dummy();

    TouchState touch{};
    VulkanContext vk{};
    PlyCloud cloud{};

    app->userData = &touch;
    app->onAppCmd = HandleCmd;
    app->onInputEvent = HandleInput;

    bool running = true;
    while (running) {
        int events;
        android_poll_source* source;
        while (ALooper_pollOnce(vk.initialized ? 0 : -1, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                running = false;
                break;
            }
        }
        if (!running) break;

        if (app->window && !vk.initialized) {
            if (!LoadPlyCloudFromAssets(app, cloud)) {
                LOGE("Proceeding without valid PLY (add app/src/main/assets/scene.ply)");
            }
            if (!InitVulkan(vk, app, cloud)) {
                LOGE("Vulkan init failed.");
                running = false;
                break;
            }
        }

        if (vk.initialized) {
            DrawFrame(vk, touch, cloud.points.size());
        }
    }

    CleanupVulkan(vk);
}

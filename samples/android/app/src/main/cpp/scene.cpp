// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT

#include "scene.h"

#include "blit_frag.spv.h"
#include "fullscreen_vert.spv.h"
#include "scene_frag.spv.h"

#include <cstring>

namespace {

VkShaderModule createModule(VkDevice device, const uint32_t* code, size_t sizeBytes)
{
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = sizeBytes;
    ci.pCode    = code;
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return module;
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask               = srcAccess;
    barrier.dstAccessMask               = dstAccess;
    barrier.oldLayout                   = oldLayout;
    barrier.newLayout                   = newLayout;
    barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                       = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

bool Scene::createTarget(VkContext& ctx, RenderTarget& rt, VkFormat format, VkExtent2D extent,
                         VkImageUsageFlags usage, VkImageAspectFlags aspect)
{
    rt.format = format;
    rt.extent = extent;

    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = format;
    ci.extent        = {extent.width, extent.height, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = usage;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(ctx.device(), &ci, nullptr, &rt.image));

    VkMemoryRequirements req = {};
    vkGetImageMemoryRequirements(ctx.device(), rt.image, &req);

    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize  = req.size;
    alloc.memoryTypeIndex = ctx.findMemoryType(req.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX) {
        LOGE("No device-local memory type for render target");
        return false;
    }
    VK_CHECK(vkAllocateMemory(ctx.device(), &alloc, nullptr, &rt.memory));
    VK_CHECK(vkBindImageMemory(ctx.device(), rt.image, rt.memory, 0));

    VkImageViewCreateInfo viewCi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewCi.image                       = rt.image;
    viewCi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    viewCi.format                      = format;
    viewCi.subresourceRange.aspectMask = aspect;
    viewCi.subresourceRange.levelCount = 1;
    viewCi.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(ctx.device(), &viewCi, nullptr, &rt.view));
    return true;
}

bool Scene::createScenePass(VkContext& ctx)
{
    VkAttachmentDescription attachments[3] = {};
    // color
    attachments[0].format         = kColorFormat;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout   = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // motion
    attachments[1] = attachments[0];
    attachments[1].format = kMotionFormat;
    // depth
    attachments[2].format         = kDepthFormat;
    attachments[2].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    // Arm ASR barriers depth resources to DEPTH_STENCIL_READ_ONLY_OPTIMAL rather
    // than SHADER_READ_ONLY_OPTIMAL whenever FFXM_RESOURCE_USAGE_DEPTHTARGET is
    // set (see addBarrier() in src/backends/vk/ffxm_vk.cpp), so hand it over in
    // that layout.
    attachments[2].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRefs[2] = {
        {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    };
    VkAttachmentReference depthRef = {2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 2;
    subpass.pColorAttachments       = colorRefs;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = 3;
    ci.pAttachments    = attachments;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dependency;
    VK_CHECK(vkCreateRenderPass(ctx.device(), &ci, nullptr, &scenePass_));

    VkImageView views[3] = {color_.view, motion_.view, depth_.view};
    VkFramebufferCreateInfo fbCi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbCi.renderPass      = scenePass_;
    fbCi.attachmentCount = 3;
    fbCi.pAttachments    = views;
    fbCi.width           = renderExtent_.width;
    fbCi.height          = renderExtent_.height;
    fbCi.layers          = 1;
    VK_CHECK(vkCreateFramebuffer(ctx.device(), &fbCi, nullptr, &sceneFramebuffer_));
    return true;
}

bool Scene::createBlitPass(VkContext& ctx)
{
    VkAttachmentDescription attachment = {};
    attachment.format         = ctx.swapchainFormat();
    attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &ref;

    VkRenderPassCreateInfo ci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = 1;
    ci.pAttachments    = &attachment;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    VK_CHECK(vkCreateRenderPass(ctx.device(), &ci, nullptr, &blitPass_));

    VkSamplerCreateInfo samplerCi = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerCi.magFilter    = VK_FILTER_LINEAR;
    samplerCi.minFilter    = VK_FILTER_LINEAR;
    samplerCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCi.maxLod       = 1.0f;
    VK_CHECK(vkCreateSampler(ctx.device(), &samplerCi, nullptr, &sampler_));

    VkDescriptorSetLayoutBinding binding = {};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setCi = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setCi.bindingCount = 1;
    setCi.pBindings    = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(ctx.device(), &setCi, nullptr, &blitSetLayout_));

    VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolCi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCi.maxSets       = 1;
    poolCi.poolSizeCount = 1;
    poolCi.pPoolSizes    = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(ctx.device(), &poolCi, nullptr, &blitPool_));

    VkDescriptorSetAllocateInfo setAlloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setAlloc.descriptorPool     = blitPool_;
    setAlloc.descriptorSetCount = 1;
    setAlloc.pSetLayouts        = &blitSetLayout_;
    VK_CHECK(vkAllocateDescriptorSets(ctx.device(), &setAlloc, &blitSet_));

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.sampler     = sampler_;
    imageInfo.imageView   = output_.view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet          = blitSet_;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imageInfo;
    vkUpdateDescriptorSets(ctx.device(), 1, &write, 0, nullptr);
    return true;
}

bool Scene::createPipelines(VkContext& ctx)
{
    VkDevice device = ctx.device();

    VkShaderModule vs = createModule(device, g_fullscreen_vert, sizeof(g_fullscreen_vert));
    VkShaderModule sceneFs = createModule(device, g_scene_frag, sizeof(g_scene_frag));
    VkShaderModule blitFs  = createModule(device, g_blit_frag, sizeof(g_blit_frag));
    if (!vs || !sceneFs || !blitFs) {
        LOGE("Failed to create sample shader modules");
        return false;
    }

    VkPipelineVertexInputStateCreateInfo vertexInput = {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState = {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster = {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynamicStates;

    // ---- scene pipeline (2 colour attachments + depth written from the shader)
    {
        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = sceneFs;
        stages[1].pName  = "main";

        VkPushConstantRange pushRange = {VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                         sizeof(ScenePushConstants)};
        VkPipelineLayoutCreateInfo layoutCi = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutCi.pushConstantRangeCount = 1;
        layoutCi.pPushConstantRanges    = &pushRange;
        VK_CHECK(vkCreatePipelineLayout(device, &layoutCi, nullptr, &sceneLayout_));

        VkPipelineColorBlendAttachmentState blendAttachments[2] = {};
        for (auto& b : blendAttachments)
            b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend = {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 2;
        blend.pAttachments    = blendAttachments;

        // Depth comes from gl_FragDepth, so the test is disabled but writes are on.
        VkPipelineDepthStencilStateCreateInfo depthStencil = {
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable  = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp   = VK_COMPARE_OP_ALWAYS;
        depthStencil.maxDepthBounds   = 1.0f;

        VkGraphicsPipelineCreateInfo ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.stageCount          = 2;
        ci.pStages             = stages;
        ci.pVertexInputState   = &vertexInput;
        ci.pInputAssemblyState = &inputAssembly;
        ci.pViewportState      = &viewportState;
        ci.pRasterizationState = &raster;
        ci.pMultisampleState   = &multisample;
        ci.pDepthStencilState  = &depthStencil;
        ci.pColorBlendState    = &blend;
        ci.pDynamicState       = &dynamic;
        ci.layout              = sceneLayout_;
        ci.renderPass          = scenePass_;
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr,
                                           &scenePipeline_));
    }

    // ---- blit pipeline
    {
        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = blitFs;
        stages[1].pName  = "main";

        VkPipelineLayoutCreateInfo layoutCi = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutCi.setLayoutCount = 1;
        layoutCi.pSetLayouts    = &blitSetLayout_;
        VK_CHECK(vkCreatePipelineLayout(device, &layoutCi, nullptr, &blitLayout_));

        VkPipelineColorBlendAttachmentState blendAttachment = {};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend = {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments    = &blendAttachment;

        VkGraphicsPipelineCreateInfo ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.stageCount          = 2;
        ci.pStages             = stages;
        ci.pVertexInputState   = &vertexInput;
        ci.pInputAssemblyState = &inputAssembly;
        ci.pViewportState      = &viewportState;
        ci.pRasterizationState = &raster;
        ci.pMultisampleState   = &multisample;
        ci.pColorBlendState    = &blend;
        ci.pDynamicState       = &dynamic;
        ci.layout              = blitLayout_;
        ci.renderPass          = blitPass_;
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr,
                                           &blitPipeline_));
    }

    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, sceneFs, nullptr);
    vkDestroyShaderModule(device, blitFs, nullptr);
    return true;
}

bool Scene::init(VkContext& ctx, VkExtent2D renderExtent, VkExtent2D displayExtent)
{
    device_        = ctx.device();
    renderExtent_  = renderExtent;
    displayExtent_ = displayExtent;

    const VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                         VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!createTarget(ctx, color_, kColorFormat, renderExtent, colorUsage,
                      VK_IMAGE_ASPECT_COLOR_BIT))
        return false;
    if (!createTarget(ctx, motion_, kMotionFormat, renderExtent, colorUsage,
                      VK_IMAGE_ASPECT_COLOR_BIT))
        return false;
    if (!createTarget(ctx, depth_, kDepthFormat, renderExtent,
                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                      VK_IMAGE_ASPECT_DEPTH_BIT))
        return false;
    // Arm ASR writes its result through a UAV, hence STORAGE.
    if (!createTarget(ctx, output_, kOutputFormat, displayExtent,
                      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT))
        return false;

    return createScenePass(ctx) && createBlitPass(ctx) && createPipelines(ctx);
}

void Scene::record(VkCommandBuffer cmd, const ScenePushConstants& pc)
{
    VkClearValue clears[3] = {};
    clears[2].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo begin = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass      = scenePass_;
    begin.framebuffer     = sceneFramebuffer_;
    begin.renderArea      = {{0, 0}, renderExtent_};
    begin.clearValueCount = 3;
    begin.pClearValues    = clears;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {0.0f, 0.0f, (float)renderExtent_.width, (float)renderExtent_.height,
                           0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, renderExtent_};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipeline_);
    vkCmdPushConstants(cmd, sceneLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(ScenePushConstants), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void Scene::barrierForUpscale(VkCommandBuffer cmd)
{
    // The render pass already left the G-buffer in SHADER_READ_ONLY_OPTIMAL, which
    // is what FFXM_RESOURCE_STATE_COMPUTE_READ maps to. Only the output needs
    // moving into GENERAL for the upscaler's storage-image write.
    imageBarrier(cmd, output_.image, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                 0, VK_ACCESS_SHADER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void Scene::recordBlit(VkCommandBuffer cmd, VkImageView swapchainView, VkExtent2D extent)
{
    imageBarrier(cmd, output_.image, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < blitFramebufferCount_; ++i) {
        if (blitFramebuffers_[i].view == swapchainView) {
            framebuffer = blitFramebuffers_[i].fb;
            break;
        }
    }
    if (framebuffer == VK_NULL_HANDLE) {
        VkFramebufferCreateInfo fbCi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbCi.renderPass      = blitPass_;
        fbCi.attachmentCount = 1;
        fbCi.pAttachments    = &swapchainView;
        fbCi.width           = extent.width;
        fbCi.height          = extent.height;
        fbCi.layers          = 1;
        if (vkCreateFramebuffer(device_, &fbCi, nullptr, &framebuffer) != VK_SUCCESS) {
            LOGE("Failed to create blit framebuffer");
            return;
        }
        if (blitFramebufferCount_ < 8)
            blitFramebuffers_[blitFramebufferCount_++] = {swapchainView, framebuffer};
    }

    VkRenderPassBeginInfo begin = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass  = blitPass_;
    begin.framebuffer = framebuffer;
    begin.renderArea  = {{0, 0}, extent};
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blitLayout_, 0, 1,
                            &blitSet_, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void Scene::destroy(VkContext& ctx)
{
    VkDevice device = ctx.device();
    if (device == VK_NULL_HANDLE)
        return;

    for (uint32_t i = 0; i < blitFramebufferCount_; ++i)
        vkDestroyFramebuffer(device, blitFramebuffers_[i].fb, nullptr);
    blitFramebufferCount_ = 0;

    vkDestroyPipeline(device, blitPipeline_, nullptr);
    vkDestroyPipelineLayout(device, blitLayout_, nullptr);
    vkDestroyDescriptorPool(device, blitPool_, nullptr);
    vkDestroyDescriptorSetLayout(device, blitSetLayout_, nullptr);
    vkDestroySampler(device, sampler_, nullptr);
    vkDestroyRenderPass(device, blitPass_, nullptr);

    vkDestroyPipeline(device, scenePipeline_, nullptr);
    vkDestroyPipelineLayout(device, sceneLayout_, nullptr);
    vkDestroyFramebuffer(device, sceneFramebuffer_, nullptr);
    vkDestroyRenderPass(device, scenePass_, nullptr);

    for (RenderTarget* rt : {&color_, &motion_, &depth_, &output_}) {
        vkDestroyImageView(device, rt->view, nullptr);
        vkDestroyImage(device, rt->image, nullptr);
        vkFreeMemory(device, rt->memory, nullptr);
        *rt = {};
    }
}

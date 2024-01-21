/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#include "DisplayManager.h"
#include "Application.h"
#include "MapManipulator.h"

using namespace ROCKY_NAMESPACE;

namespace
{
    // Call this when adding a new rendergraph to the scene.
    void activateRenderGraph(
        vsg::ref_ptr<vsg::RenderGraph> renderGraph,
        vsg::ref_ptr<vsg::Window> window,
        vsg::ref_ptr<vsg::Viewer> viewer)
    {
        vsg::ref_ptr<vsg::View> view;

        if (!renderGraph->children.empty())
        {
            view = renderGraph->children[0].cast<vsg::View>();
        }

        if (view)
        {
            // add this rendergraph's view to the viewer's compile manager.
            viewer->compileManager->add(*window, view);

            // Compile the new render pass for this view.
            // The lambda idiom is taken from vsgexamples/dynamicviews
            auto result = viewer->compileManager->compile(renderGraph, [&view](vsg::Context& context)
                {
                    return context.view == view.get();
                });

            // if something was compiled, we need to update the viewer:
            if (result.requiresViewerUpdate())
            {
                vsg::updateViewer(*viewer, result);
            }
        }
    }


    // https://github.com/KhronosGroup/Vulkan-Samples/tree/main/samples/extensions/debug_utils
    VKAPI_ATTR VkBool32 VKAPI_CALL debug_utils_messenger_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
        VkDebugUtilsMessageTypeFlagsEXT message_type,
        const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
        void* user_data)
    {
        if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            Log()->warn("\n" + std::string(callback_data->pMessage));
        }
        else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            Log()->warn("\n" + std::string(callback_data->pMessage));
        }
        return VK_FALSE;
    }
}



DisplayManager::DisplayManager(Application& in_app) :
    app(in_app)
{
}

void
DisplayManager::addWindow(vsg::ref_ptr<vsg::Window> window)
{
    ROCKY_SOFT_ASSERT_AND_RETURN(window, void());
    ROCKY_SOFT_ASSERT_AND_RETURN(app.mapNode.valid(), void());

    // Each window gets its own CommandGraph. We will store it here and then
    // set it up later when the frame loop starts.
    auto commandgraph = vsg::CommandGraph::create(window);
    _commandGraphByWindow[window] = commandgraph;

    // main camera
    double nearFarRatio = 0.00001;
    double R = app.mapNode->mapSRS().ellipsoid().semiMajorAxis();
    double ar = (double)window->extent2D().width / (double)window->extent2D().height;

    auto camera = vsg::Camera::create(
        vsg::Perspective::create(30.0, ar, R * nearFarRatio, R * 20.0),
        vsg::LookAt::create(),
        vsg::ViewportState::create(0, 0, window->extent2D().width, window->extent2D().height));

    auto view = vsg::View::create(camera, app.mainScene);

    addViewToWindow(view, window, {});

    // Tell Rocky it needs to mutex-protect the terrain engine
    // now that we have more than one window.
    if (app.viewer->windows().size() > 1)
    {
        app.mapNode->terrainSettings().supportMultiThreadedRecord = true;
    }

    // add the new window to our viewer
    app.viewer->addWindow(window);

    // install a manipulator for the new view:
    addManipulator(window, view);

    if (app._viewerRealized)
    {
        app._viewerDirty = true;
    }

    // install the debug layer if requested
    if (app._debuglayer && !_debugCallbackInstalled)
    {
        VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        debug_utils_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        debug_utils_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        debug_utils_create_info.pfnUserCallback = debug_utils_messenger_callback;

        static VkDebugUtilsMessengerEXT debug_utils_messenger;

        auto vki = window->getDevice()->getInstance();

        using PFN_vkCreateDebugUtilsMessengerEXT = VkResult(VKAPI_PTR*)(VkInstance, const VkDebugUtilsMessengerCreateInfoEXT*, const VkAllocationCallbacks*, VkDebugUtilsMessengerEXT*);
        PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
        if (vki->getProcAddr(vkCreateDebugUtilsMessengerEXT, "vkCreateDebugUtilsMessenger", "vkCreateDebugUtilsMessengerEXT"))
        {
            vkCreateDebugUtilsMessengerEXT(vki->vk(), &debug_utils_create_info, nullptr, &debug_utils_messenger);
        }

        _debugCallbackInstalled = true;
    }
}

vsg::ref_ptr<vsg::Window>
DisplayManager::addWindow(vsg::ref_ptr<vsg::WindowTraits> traits)
{
    ROCKY_SOFT_ASSERT_AND_RETURN(traits, {});

    // wait until the device is idle to avoid changing state while it's being used.
    app.viewer->deviceWaitIdle();

    //viewer->stopThreading();

    traits->debugLayer = app._debuglayer;
    traits->apiDumpLayer = app._apilayer;
    if (!app._vsync)
    {
        traits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    // This will install the debug messaging callback so we can capture validation errors
    traits->instanceExtensionNames.push_back("VK_EXT_debug_utils");

    // This is required to use the NVIDIA barycentric extension without validation errors
    if (!traits->deviceFeatures)
    {
        traits->deviceFeatures = vsg::DeviceFeatures::create();
    }
    traits->deviceExtensionNames.push_back(VK_NV_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME);
    auto& bary = traits->deviceFeatures->get<VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR>();
    bary.fragmentShaderBarycentric = true;

    // share the device across all windows
    if (app.viewer->windows().size() > 0)
    {
        traits->device = app.viewer->windows().front()->getDevice();
    }

    auto window = vsg::Window::create(traits);

    addWindow(window);

    return window;
}

void
DisplayManager::addViewToWindow(
    vsg::ref_ptr<vsg::View> view,
    vsg::ref_ptr<vsg::Window> window,
    std::function<void(vsg::CommandGraph*)> on_create)
{
    ROCKY_SOFT_ASSERT_AND_RETURN(window != nullptr, void());
    ROCKY_SOFT_ASSERT_AND_RETURN(view != nullptr, void());
    ROCKY_SOFT_ASSERT_AND_RETURN(view->camera != nullptr, void());

    if (app._viewerRealized)
    {
        app.viewer->deviceWaitIdle();
    }

    auto commandgraph = getCommandGraph(window);
    if (commandgraph)
    {
        if (view->children.empty())
        {
            view->addChild(app.root);
        }

        auto rendergraph = vsg::RenderGraph::create(window, view);
        rendergraph->setClearValues({ {0.1f, 0.12f, 0.15f, 1.0f} });
        commandgraph->addChild(rendergraph);

        if (app._viewerRealized)
        {
            activateRenderGraph(rendergraph, window, app.viewer);
        }

        auto& viewdata = _viewData[view];
        viewdata.parentRenderGraph = rendergraph;

        windows[window].emplace_back(view);

        addManipulator(window, view);
    }
}

void
DisplayManager::removeView(vsg::ref_ptr<vsg::View> view)
{
    // wait until the device is idle to avoid changing state while it's being used.
    app.viewer->deviceWaitIdle();

    auto window = getWindow(view);
    ROCKY_SOFT_ASSERT_AND_RETURN(window != nullptr, void());

    auto commandgraph = getCommandGraph(window);
    ROCKY_SOFT_ASSERT_AND_RETURN(commandgraph, void());

    // find the rendergraph hosting the view:
    auto vd = _viewData.find(view);
    ROCKY_SOFT_ASSERT_AND_RETURN(vd != _viewData.end(), void());
    auto& rendergraph = vd->second.parentRenderGraph;

    // remove the rendergraph from the command graph.
    auto& rps = commandgraph->children;
    rps.erase(std::remove(rps.begin(), rps.end(), rendergraph), rps.end());

    // remove it from our tracking tables.
    _viewData.erase(view);
    auto& views = windows[vsg::observer_ptr<vsg::Window>(window)];
    views.erase(std::remove(views.begin(), views.end(), view), views.end());
}

void
DisplayManager::refreshView(vsg::ref_ptr<vsg::View> view)
{
    ROCKY_SOFT_ASSERT_AND_RETURN(view, void());

    auto& viewdata = _viewData[view];
    ROCKY_SOFT_ASSERT_AND_RETURN(viewdata.parentRenderGraph, void());

    // wait until the device is idle to avoid changing state while it's being used.
    app.viewer->deviceWaitIdle();

    auto vp = view->camera->getViewport();
    viewdata.parentRenderGraph->renderArea.offset.x = (std::uint32_t)vp.x;
    viewdata.parentRenderGraph->renderArea.offset.y = (std::uint32_t)vp.y;
    viewdata.parentRenderGraph->renderArea.extent.width = (std::uint32_t)vp.width;
    viewdata.parentRenderGraph->renderArea.extent.height = (std::uint32_t)vp.height;

    // rebuild the graphics pipelines to reflect new camera/view params.
    vsg::UpdateGraphicsPipelines u;
    u.context = vsg::Context::create(viewdata.parentRenderGraph->getRenderPass()->device);
    u.context->renderPass = viewdata.parentRenderGraph->getRenderPass();
    viewdata.parentRenderGraph->accept(u);
}

DisplayManager::ViewData&
DisplayManager::viewData(vsg::ref_ptr<vsg::View> view)
{
    return _viewData[view];
}

vsg::ref_ptr<vsg::CommandGraph>
DisplayManager::getCommandGraph(vsg::ref_ptr<vsg::Window> window)
{
    auto iter = _commandGraphByWindow.find(window);
    if (iter != _commandGraphByWindow.end())
        return iter->second;
    else
        return {};
}

vsg::ref_ptr<vsg::Window>
DisplayManager::getWindow(vsg::ref_ptr<vsg::View> view)
{
    for (auto iter : windows)
    {
        for (auto& a_view : iter.second)
        {
            if (a_view == view)
            {
                return iter.first;
                break;
            }
        }
    }
    return {};
}

void
DisplayManager::addPreRenderGraph(vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::RenderGraph> renderGraph)
{
    auto commandGraph = getCommandGraph(window);

    ROCKY_SOFT_ASSERT_AND_RETURN(commandGraph, void());
    ROCKY_SOFT_ASSERT_AND_RETURN(commandGraph->children.size() > 0, void());

    // Insert the pre-render graph into the command graph.
    commandGraph->children.insert(commandGraph->children.begin(), renderGraph);

    // hook it up.
    activateRenderGraph(renderGraph, window, app.viewer);
}

void
DisplayManager::addManipulator(vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::View> view)
{
    auto manip = MapManipulator::create(app.mapNode, window, view->camera);

    // stow this away in the view object so it's easy to find later.
    view->setObject(MapManipulator::tag, manip);

    // The manipulators (one for each view) need to be in the right order (top to bottom)
    // so that overlapping views don't get mixed up. To accomplish this we'll just
    // remove them all and re-insert them in the new proper order:
    auto& ehs = app.viewer->getEventHandlers();

    // remove all the MapManipulators using the dumb remove-erase idiom
    ehs.erase(
        std::remove_if(
            ehs.begin(), ehs.end(),
            [](const vsg::ref_ptr<vsg::Visitor>& v) { return dynamic_cast<MapManipulator*>(v.get()); }),
        ehs.end()
    );

    // re-add them in the right order (last to first)
    for (auto& window : windows)
    {
        for (auto vi = window.second.rbegin(); vi != window.second.rend(); ++vi)
        {
            auto& view = *vi;
            auto manip = view->getRefObject<MapManipulator>(MapManipulator::tag);
            ehs.push_back(manip);
        }
    }
}
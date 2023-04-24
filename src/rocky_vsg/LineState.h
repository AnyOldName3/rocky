/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once

#include <rocky_vsg/Common.h>

#include <vsg/io/Options.h>
#include <vsg/utils/GraphicsPipelineConfigurator.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/Geometry.h>
#include <vsg/commands/DrawIndexed.h>

namespace ROCKY_NAMESPACE
{
    class Runtime;

    //! Settings when constructing a similar set of line drawables
    //! Note, this structure is mirrored on the GPU so alignment rules apply!
    struct LineStyle
    {
        // if alpha is zero, use the line's per-vertex color instead
        vsg::vec4 color = { 1, 1, 1, 0 };
        float width = 2.0f;
        unsigned int stipple_pattern = 0xffff;
        unsigned int stipple_factor = 1;
    };

    namespace engine
    {
        /**
         * Creates commands for rendering line primitives and holds the singleton pipeline
         * configurator for line drawing state.
         */
        class ROCKY_VSG_EXPORT LineState
        {
        public:
            //! Construct the Line state generator and initialize its pipeline configurator
            ~LineState();

            //! Create the state commands necessary for rendering lines.
            //! Upi should add these to an existing StateGroup.
            static vsg::StateGroup::StateCommands createPipelineStateCommands(Runtime&);

            //! Singleton pipeline conifig object created when the object is first constructed.
            static vsg::ref_ptr<vsg::GraphicsPipelineConfigurator> pipelineConfig;
        };

        /**
        * Applies a line style to any LineStringNode children.
        */
        class ROCKY_VSG_EXPORT LineStringStyleNode : public vsg::Inherit<vsg::StateGroup, LineStringStyleNode>
        {
        public:
            //! Construct a line style node
            LineStringStyleNode();

            //! Style for any linestrings that are children of this node
            void setStyle(const LineStyle&);
            const LineStyle& style() const;

            void compile(vsg::Context&) override;

        private:
            vsg::ref_ptr<vsg::ubyteArray> _buffer;
        };

        /**
        * Renders a line or linestring geometry.
        */
        class ROCKY_VSG_EXPORT LineStringGeometry : public vsg::Inherit<vsg::Geometry, LineStringGeometry>
        {
        public:
            //! Construct a new line string geometry node
            LineStringGeometry();

            //! Adds a vertex to the end of the line string
            void push_back(const vsg::vec3& vert);

            //! Number of verts comprising this line string
            unsigned numVerts() const;

            //! The first vertex in the line string to render
            void setFirst(unsigned value);

            //! Number of vertices in the line string to render
            void setCount(unsigned value);

            //! Recompile the geometry after making changes.
            void compile(vsg::Context&) override;

        protected:
            vsg::vec4 _defaultColor = { 1,1,1,1 };
            std::uint32_t _stippleFactor;
            std::uint16_t _stipplePattern;
            float _width;
            std::vector<vsg::vec3> _current;
            std::vector<vsg::vec3> _previous;
            std::vector<vsg::vec3> _next;
            std::vector<vsg::vec4> _colors;
            vsg::ref_ptr<vsg::DrawIndexed> _drawCommand;

            //unsigned actualVertsPerVirtualVert(unsigned) const;
            //unsigned numVirtualVerts(const vsg::Array*) const;
            //unsigned getRealIndex(unsigned) const;
            //void updateFirstCount();
        };
    }
}
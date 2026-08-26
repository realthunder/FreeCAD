
target_sources(
    Part_tests_run
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/TopoShape.cpp
            # TopoShapeCache.cpp tests upstream's src/Mod/Part/App/TopoShapeCache
            # class, which this fork does not build -- the two source files are
            # commented out of src/Mod/Part/App/CMakeLists.txt because the fork
            # keeps its own cache and ShapeRelationKey inside TopoShapeEx.cpp,
            # file-local. So Part.so exports neither symbol and the suite cannot
            # link. Re-enable this together with that source, never before.
            # ${CMAKE_CURRENT_SOURCE_DIR}/TopoShapeCache.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/FeaturePartCommon.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/FeaturePartCut.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/FeaturePartFuse.cpp
            # ${CMAKE_CURRENT_SOURCE_DIR}/FeatureFillet.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/PartTestHelpers.cpp
)

/*
 * SPDX-FileCopyrightText: Copyright (c) 1993-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <numeric>
#include <stdexcept>
#include "disentangledAttentionPlugin.h"
#include "NvInferPlugin.h"
#include <cuda_fp16.h>


using namespace nvinfer1;
using nvinfer1::plugin::DisentangledAttentionPlugin;
using nvinfer1::plugin::DisentangledAttentionPluginCreator;


// Static class fields initialization
PluginFieldCollection DisentangledAttentionPluginCreator::mFC{};
std::vector<PluginField> DisentangledAttentionPluginCreator::mPluginAttributes;

REGISTER_TENSORRT_PLUGIN(DisentangledAttentionPluginCreator);

#define CHECK_CUDNN(call)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        cudnnStatus_t status = call;                                                                                   \
        if (status != CUDNN_STATUS_SUCCESS)                                                                            \
        {                                                                                                              \
            return status;                                                                                             \
        }                                                                                                              \
    } while (0)

namespace
{
constexpr const char* DEBERTA_NAME{"DisentangledAttention_TRT"};
constexpr const char* DEBERTA_VERSION{"1"};
} // namespace

DisentangledAttentionPlugin::DisentangledAttentionPlugin()
{
}

DisentangledAttentionPlugin::DisentangledAttentionPlugin(int span, float factor)
    : mSpan(span),
      mFactor(factor)
{
}

DisentangledAttentionPlugin::DisentangledAttentionPlugin(void const* serialData, size_t serialLength)
{
    // Deserialize in the same order as serialization
    deserialize_value(&serialData, &serialLength, &mSpan);
    deserialize_value(&serialData, &serialLength, &mFactor);
}

DisentangledAttentionPlugin::~DisentangledAttentionPlugin()
{
    terminate();
}

int DisentangledAttentionPlugin::getNbOutputs() const noexcept
{
    return 1;
}

int DisentangledAttentionPlugin::initialize() noexcept
{
    // if need large amount of GPU memory, recommend to specify in getWorkspaceSize so TRT allocates it. If not, when a plugin is called many times, the memory manually allocated by this initialize() is repeated many times -- may overflow
    return 0;
}

const char* DisentangledAttentionPlugin::getPluginType() const noexcept
{
    return DEBERTA_NAME;
}

const char* DisentangledAttentionPlugin::getPluginVersion() const noexcept
{
    return DEBERTA_VERSION;
}

// IPluginV2DynamicExt Methods
nvinfer1::DimsExprs DisentangledAttentionPlugin::getOutputDimensions(
    int index, const nvinfer1::DimsExprs* inputs, int nbInputs, nvinfer1::IExprBuilder& exprBuilder) noexcept
{
    nvinfer1::DimsExprs output;
    if constexpr(VERSION == 1) {
        ASSERT(nbInputs == 4); // 4 inputs
        output = inputs[1]; // same as input[1] or input[3], i.e. index1 or index2
    }
    else if constexpr (VERSION == 2) {
        ASSERT(nbInputs == 3); // 3 inputs
        output = inputs[0]; // same as input[0], i.e. data0
    }

    ASSERT(index < 1); // only one output

    return output;
}

void DisentangledAttentionPlugin::attachToContext(cudnnContext* cudnnContext, cublasContext* cublasContext, IGpuAllocator* gpuAllocator) noexcept
{
}

// Detach the plugin object from its execution context.
void DisentangledAttentionPlugin::detachFromContext() noexcept
{
}

template<typename DataType>
DataType const * DisentangledAttentionPlugin::pointer_const_cast(const void * const p)
{
    return static_cast<DataType const *>(p);
}

template<typename DataType>
DataType * DisentangledAttentionPlugin::pointer_cast(void * const p)
{
    return static_cast<DataType *>(p);
}

int DisentangledAttentionPlugin::enqueue(const nvinfer1::PluginTensorDesc* inputDesc,
    const nvinfer1::PluginTensorDesc* outputDesc, const void* const* inputs, void* const* outputs, void* workspace,
    cudaStream_t stream) noexcept
{
    if constexpr (VERSION == 1) {
        nvinfer1::Dims dims0 = inputDesc[0].dims;
        nvinfer1::Dims dims1 = inputDesc[1].dims;
        nvinfer1::Dims dims2 = inputDesc[2].dims;
        nvinfer1::Dims dims3 = inputDesc[3].dims;
        dim3 dim_data1(dims0.d[0], dims0.d[1], dims0.d[2]);
        dim3 dim_index1(dims1.d[0], dims1.d[1], dims1.d[2]);
        dim3 dim_data2(dims2.d[0], dims2.d[1], dims2.d[2]);
        dim3 dim_index2(dims3.d[0], dims3.d[1], dims3.d[2]);
        dim3 dim_result(dim_index2);

        dim3 block_optimized( TileSize, BlockDimY );
        dim3 grid_optimized( (dim_result.z-1)/TileSize+1, (dim_result.y-1)/TileSize+1, dim_result.x);

        __half const *d_data1 = static_cast<__half const *>(inputs[0]);
        int const *d_index1 = static_cast<int const *>(inputs[1]);
        __half const *d_data2 = static_cast<__half const *>(inputs[2]);
        int const * d_index2 = static_cast<int const *>(inputs[3]);
        __half *d_result = static_cast<__half*>(outputs[0]);

        disentangled_kernel_wrapper_v1<__half>(d_data1, d_index1, d_data2, d_index2, d_result, dim_data1, dim_index1, dim_data2, dim_index2, dim_result, block_optimized, grid_optimized, stream);
    }
    else if constexpr (VERSION == 2){
        nvinfer1::Dims dims0 = inputDesc[0].dims;
        nvinfer1::Dims dims1 = inputDesc[1].dims;
        nvinfer1::Dims dims2 = inputDesc[2].dims;
        dim3 dim_data0(dims0.d[0], dims0.d[1], dims0.d[2]);
        dim3 dim_data1(dims1.d[0], dims1.d[1], dims1.d[2]);
        dim3 dim_data2(dims2.d[0], dims2.d[1], dims2.d[2]);
        dim3 dim_result(dim_data0);

        dim3 block_optimized( TileSize, BlockDimY );
        dim3 grid_optimized( (dim_result.z-1)/TileSize+1, (dim_result.y-1)/TileSize+1, dim_result.x);

        if (inputDesc[0].type == nvinfer1::DataType::kFLOAT)
        {
            auto const *d_data0 = pointer_const_cast<float>(inputs[0]);
            auto const *d_data1 = pointer_const_cast<float>(inputs[1]);
            auto const *d_data2 = pointer_const_cast<float>(inputs[2]);
            auto *d_result = pointer_cast<float>(outputs[0]);
            disentangled_kernel_wrapper_v2<float, TileSize, BlockDimY>(d_data0, d_data1, d_data2, d_result, dim_data0, dim_data1, dim_data2, dim_result, mFactor, mSpan, block_optimized, grid_optimized, stream);
        }
        else if (inputDesc[0].type == nvinfer1::DataType::kHALF) {
            auto const *d_data0 = pointer_const_cast<__half>(inputs[0]);
            auto const *d_data1 = pointer_const_cast<__half>(inputs[1]);
            auto const *d_data2 = pointer_const_cast<__half>(inputs[2]);
            auto *d_result = pointer_cast<__half>(outputs[0]);
            __half factor = __float2half(mFactor);
            disentangled_kernel_wrapper_v2<__half, TileSize, BlockDimY>(d_data0, d_data1, d_data2, d_result, dim_data0, dim_data1, dim_data2, dim_result, factor, mSpan, block_optimized, grid_optimized, stream);
        }
        else if (inputDesc[0].type == nvinfer1::DataType::kINT8) {
            auto const *d_data0 = pointer_const_cast<int8_t>(inputs[0]);
            auto const *d_data1 = pointer_const_cast<int8_t>(inputs[1]);
            auto const *d_data2 = pointer_const_cast<int8_t>(inputs[2]);
            auto *d_result = pointer_cast<int8_t>(outputs[0]);
            int8_t factor = int8_t(mFactor);
            disentangled_kernel_wrapper_v2<int8_t, TileSize, BlockDimY>(d_data0, d_data1, d_data2, d_result, dim_data0, dim_data1, dim_data2, dim_result, factor, mSpan, block_optimized, grid_optimized, stream);
        }
    }

    return cudaPeekAtLastError();  
}

size_t DisentangledAttentionPlugin::getSerializationSize() const noexcept
{
    return sizeof(mSpan) + sizeof(mFactor);
}

void DisentangledAttentionPlugin::serialize(void* buffer) const noexcept
{
    serialize_value(&buffer, mSpan);
    serialize_value(&buffer, mFactor);
}

bool DisentangledAttentionPlugin::supportsFormatCombination(
    int pos, const nvinfer1::PluginTensorDesc* inOut, int nbInputs, int nbOutputs) noexcept
{
    
    ASSERT(inOut && pos < (nbInputs + nbOutputs));

    const bool consistentFloatPrecision = (inOut[pos].type == inOut[0].type); // all inputs & outputs should have the same precision type

    // 3 inputs, 1 output
    switch (pos)
    {
    case 0: 
        return (inOut[pos].type == nvinfer1::DataType::kINT8 || inOut[pos].type == nvinfer1::DataType::kHALF || inOut[pos].type == nvinfer1::DataType::kFLOAT) && inOut[pos].format == nvinfer1::PluginFormat::kLINEAR && consistentFloatPrecision; // linear means row-major ordering
    case 1:
        return (inOut[pos].type == nvinfer1::DataType::kINT8 || inOut[pos].type == nvinfer1::DataType::kHALF || inOut[pos].type == nvinfer1::DataType::kFLOAT) && inOut[pos].format == nvinfer1::PluginFormat::kLINEAR && consistentFloatPrecision;
    case 2:
        return (inOut[pos].type == nvinfer1::DataType::kINT8 || inOut[pos].type == nvinfer1::DataType::kHALF || inOut[pos].type == nvinfer1::DataType::kFLOAT) && inOut[pos].format == nvinfer1::PluginFormat::kLINEAR && consistentFloatPrecision;
    case 3:
        return (inOut[pos].type == nvinfer1::DataType::kINT8 || inOut[pos].type == nvinfer1::DataType::kHALF || inOut[pos].type == nvinfer1::DataType::kFLOAT) && inOut[pos].format == nvinfer1::PluginFormat::kLINEAR && consistentFloatPrecision;
    }
    // types: kFLOAT, kHALF, kINT32, kINT8
    return false;
}

void DisentangledAttentionPlugin::terminate() noexcept
{
}

void DisentangledAttentionPlugin::destroy() noexcept
{
    // This gets called when the network containing plugin is destroyed
    delete this;
}

IPluginV2DynamicExt* DisentangledAttentionPlugin::clone() const noexcept
{
    auto* plugin = new DisentangledAttentionPlugin(mSpan, mFactor);
    plugin->setPluginNamespace(mPluginNamespace);
    return plugin;
}

void DisentangledAttentionPlugin::configurePlugin(const nvinfer1::DynamicPluginTensorDesc* in, int nbInputs,
    const nvinfer1::DynamicPluginTensorDesc* out, int nbOutputs) noexcept
{
    if constexpr(VERSION == 1) {
        // inputs
        ASSERT(nbInputs == 4); // 4 inputs
        
        // check for valid input dimensions
        ASSERT(in[0].desc.dims.nbDims == 3);
        ASSERT(in[1].desc.dims.nbDims == 3);
        ASSERT(in[2].desc.dims.nbDims == 3);
        ASSERT(in[3].desc.dims.nbDims == 3);

        // check BN (batch_size * num_heads) dimension consistency
        ASSERT(in[0].desc.dims.d[0] == in[1].desc.dims.d[0]);
        ASSERT(in[0].desc.dims.d[0] == in[2].desc.dims.d[0]);
        ASSERT(in[0].desc.dims.d[0] == in[3].desc.dims.d[0]);

        // check S (sequence_length) dimension consistency
        ASSERT(in[0].desc.dims.d[1] == in[1].desc.dims.d[1]);
        ASSERT(in[0].desc.dims.d[1] == in[2].desc.dims.d[1]);
        ASSERT(in[0].desc.dims.d[1] == in[3].desc.dims.d[1]);
        ASSERT(in[1].desc.dims.d[1] == in[1].desc.dims.d[2]);
        ASSERT(in[3].desc.dims.d[1] == in[3].desc.dims.d[2]);

        // check K (2 * span) dimension consistency for in[0] and in[2]
        ASSERT(in[0].desc.dims.d[2] == 2 * mSpan);
        ASSERT(in[2].desc.dims.d[2] == 2 * mSpan);

        // Outputs (same dimension as in[1])
        ASSERT(nbOutputs == 1);
        ASSERT(out[0].desc.dims.nbDims == 3);
        ASSERT(in[1].desc.dims.d[0] == out[0].desc.dims.d[0]);
        ASSERT(in[1].desc.dims.d[1] == out[0].desc.dims.d[1]);
        ASSERT(in[1].desc.dims.d[2] == out[0].desc.dims.d[2]);
    }
    else if constexpr (VERSION == 2) {
        // inputs
        ASSERT(nbInputs == 3); // 3 inputs
        
        // check for valid input dimensions
        ASSERT(in[0].desc.dims.nbDims == 3);
        ASSERT(in[1].desc.dims.nbDims == 3);
        ASSERT(in[2].desc.dims.nbDims == 3);

        // check BN (batch_size * num_heads) dimension consistency
        ASSERT(in[0].desc.dims.d[0] == in[1].desc.dims.d[0]);
        ASSERT(in[0].desc.dims.d[0] == in[2].desc.dims.d[0]);

        // check S (sequence_length) dimension consistency
        ASSERT(in[0].desc.dims.d[1] == in[1].desc.dims.d[1]);
        ASSERT(in[0].desc.dims.d[1] == in[2].desc.dims.d[1]);
        ASSERT(in[0].desc.dims.d[1] == in[0].desc.dims.d[2]);

        // check K (2 * span) dimension consistency for in[1] and in[2]
        ASSERT(in[1].desc.dims.d[2] == 2 * mSpan);
        ASSERT(in[2].desc.dims.d[2] == 2 * mSpan);

        // Outputs (same dimension as in[0])
        ASSERT(nbOutputs == 1);
        ASSERT(out[0].desc.dims.nbDims == 3);
        ASSERT(in[0].desc.dims.d[0] == out[0].desc.dims.d[0]);
        ASSERT(in[0].desc.dims.d[1] == out[0].desc.dims.d[1]);
        ASSERT(in[0].desc.dims.d[2] == out[0].desc.dims.d[2]);
    }

}

nvinfer1::DataType DisentangledAttentionPlugin::getOutputDataType(
    int index, const nvinfer1::DataType* inputTypes, int nbInputs) const noexcept
{
    ASSERT(inputTypes && nbInputs > 0 && index < 1);
    return inputTypes[0]; // version 1, same as data1; version 2, same as data0
}

size_t DisentangledAttentionPlugin::getWorkspaceSize(const nvinfer1::PluginTensorDesc* inputs, int nbInputs,
    const nvinfer1::PluginTensorDesc* outputs, int nbOutputs) const noexcept
{
    return 0;
}

void DisentangledAttentionPlugin::setPluginNamespace(const char* libNamespace) noexcept
{
    mPluginNamespace = libNamespace;
}

const char* DisentangledAttentionPlugin::getPluginNamespace() const noexcept
{
    return mPluginNamespace;
}

DisentangledAttentionPluginCreator::DisentangledAttentionPluginCreator()
{
    mPluginAttributes.clear();

    // consistent with the ONNX model attr fields
    mPluginAttributes.emplace_back(PluginField("span", nullptr, PluginFieldType::kINT32, 1));
    mPluginAttributes.emplace_back(PluginField("factor", nullptr, PluginFieldType::kFLOAT32, 1));

    mFC.nbFields = mPluginAttributes.size();
    mFC.fields = mPluginAttributes.data();
}

const char* DisentangledAttentionPluginCreator::getPluginName() const noexcept
{
    return DEBERTA_NAME;
}

const char* DisentangledAttentionPluginCreator::getPluginVersion() const noexcept
{
    return DEBERTA_VERSION;
}

const PluginFieldCollection* DisentangledAttentionPluginCreator::getFieldNames() noexcept
{
    return &mFC;
}

const char* DisentangledAttentionPluginCreator::getPluginNamespace() const noexcept
{
    return mNamespace.c_str();
}

void DisentangledAttentionPluginCreator::setPluginNamespace(const char* libNamespace) noexcept
{
    mNamespace = libNamespace;
}

IPluginV2DynamicExt* DisentangledAttentionPluginCreator::createPlugin(const char* name, const PluginFieldCollection* fc) noexcept
{
    // Set default values
    int span{1};
    float factor{0.00001F};
    for (int i = 0; i < fc->nbFields; i++)
    {
        std::string field_name(fc->fields[i].name);
        if (field_name.compare("span") == 0)
        {
            span = *static_cast<const int*>(fc->fields[i].data);
        }
        if (field_name.compare("factor") == 0)
        {
            factor = *static_cast<const float*>(fc->fields[i].data);
        }
    }

    DisentangledAttentionPlugin* plugin = new DisentangledAttentionPlugin(span, factor);
    plugin->setPluginNamespace(mNamespace.c_str());

    return plugin;
}

IPluginV2DynamicExt* DisentangledAttentionPluginCreator::deserializePlugin(const char* name, const void* serialData, size_t serialLength) noexcept
{
    DisentangledAttentionPlugin* plugin = new DisentangledAttentionPlugin(serialData, serialLength);
    plugin->setPluginNamespace(mNamespace.c_str());

    return plugin;
}

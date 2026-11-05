// required by TinyGLTF
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "TinyGLTF/tiny_gltf.h"
#include "Materials/TextureUtils.h"

using namespace tinygltf;



// minimalistic code to draw a single triangle, this is not part of the API.
#include "shaderc/shaderc.h" // needed for compiling shaders at runtime
#ifdef _WIN32 // must use MT platform DLL libraries on windows
#pragma comment(lib, "shaderc_combined.lib") 
#endif

void PrintLabeledDebugString(const char* label, const char* toPrint)
{
	std::cout << label << toPrint << std::endl;

	//OutputDebugStringA is a windows-only function 
#if defined WIN32 
	OutputDebugStringA(label);
	OutputDebugStringA(toPrint);
#endif
}

class Renderer
{
	// proxy handles
	GW::SYSTEM::GWindow win;
	GW::GRAPHICS::GVulkanSurface vlk;
	VkRenderPass renderPass;
	GW::CORE::GEventReceiver shutdown;

	// what we need at a minimum to draw a triangle
	VkDevice device = nullptr;
	VkPhysicalDevice physicalDevice = nullptr;
	VkBuffer geometryHandle = nullptr;
	VkDeviceMemory geometryData = nullptr;
	VkShaderModule vertexShader = nullptr;
	VkShaderModule fragmentShader = nullptr;
	VkPipeline pipeline = nullptr;
	VkPipelineLayout pipelineLayout = nullptr;

	Model model;
	TinyGLTF loader;
	std::string err;
	std::string warn;

	VkDescriptorSetLayout descriptorSetLayout = nullptr;
	VkDescriptorPool descriptorPool = nullptr;
	std::vector<VkDescriptorSet> descriptorSet;
	std::vector<VkBuffer> uniformBuffers;
	std::vector<VkDeviceMemory> uniformBuffersData;

	std::vector<VkBuffer> storageBuffers;
	std::vector<VkDeviceMemory> storageBuffersData;

	std::vector<uint8_t> geom;

	std::array<VkDeviceSize, 4> vertexBindingOffsets{};
	VkDeviceSize indexStartOffset = 0;

	unsigned int windowWidth, windowHeight;


	GW::MATH::GMatrix gMatrixProxy;

	struct MATRIX_VARS
	{
		GW::MATH::GVECTORF lightDirection, lightColor, sunAmbient, camPos;
		GW::MATH::GMATRIXF worldMatrix, viewMatrix, projMatrix;
	};
	MATRIX_VARS matrixVars;
	GW::MATH::GMATRIXF worldMatrix = GW::MATH::GIdentityMatrixF;
	GW::MATH::GMATRIXF viewMatrix;
	GW::MATH::GMATRIXF projMatrix;

	GW::INPUT::GInput input;
	GW::INPUT::GController controller;

	GW::MATH::GVECTORF lightDir = { 1.0f, -1.0f, -1.0f };
	GW::MATH::GVECTORF lightColor = { 0.9f, 0.9f, 1.0f, 1.0f };
	GW::MATH::GVECTORF ambientLight = { 0.25f, 0.25f, 0.35f };

	struct TextureData
	{
		VkBuffer buffer;
		VkDeviceMemory memory;
		VkImage image;
		VkImageView imageView;
	};
	std::vector<TextureData> textures;


	VkDescriptorSetLayout textureDescriptorSetLayout;
	VkDescriptorSet textureDescriptorSet;
	VkSampler sampleTexture;


public:
	Renderer(GW::SYSTEM::GWindow _win, GW::GRAPHICS::GVulkanSurface _vlk)
	{
		win = _win;
		vlk = _vlk;

		gMatrixProxy.Create();
		UpdateWindowDimensions();
		GetHandlesFromSurface();

		matrixVars.worldMatrix = worldMatrix;

		ViewMatrixInit();
		matrixVars.viewMatrix = viewMatrix;

		ProjectionMatrixInit();
		matrixVars.projMatrix = projMatrix;

		matrixVars.lightColor = lightColor;
		matrixVars.lightDirection = lightDir;
		matrixVars.sunAmbient = ambientLight;

		input.Create(win);
		controller.Create();

		LoadGLTFFile();
		LoadTextureFromGLTFFile();

		InitializeGraphics();
		BindShutdownCallback();

	}

private:
	void UpdateWindowDimensions()
	{
		win.GetClientWidth(windowWidth);
		win.GetClientHeight(windowHeight);
	}

	void InitializeGraphics()
	{
		DescriptorLayoutInit();
		InitializeGeometryBuffer();

		UniformBufferInit();
		StorageBufferInit();

		DescriptorPoolInit();
		DescriptorSetInit();
		LinkDescriptorSetToUniformBuffer();

		CompileShaders();
		InitializeGraphicsPipeline();
	}

	void GetHandlesFromSurface()
	{
		vlk.GetDevice((void**)&device);
		vlk.GetPhysicalDevice((void**)&physicalDevice);
		vlk.GetRenderPass((void**)&renderPass);
	}

	void LoadGLTFFile()
	{
		bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "C:\\Users\\hype\\Documents\\GitHub\\3dcc-lab-6-202510-parkerhamell\\Models\\Fish_Blender.gltf");
		//bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "C:\\Users\\hype\\Documents\\GitHub\\3dcc-lab-6-202510-parkerhamell\\Models\\Bebe_blender.gltf");
		
		std::string mode;
		std::string accType;

		if (!warn.empty()) {
			printf("Warn: %s\n", warn.c_str());
		}

		if (!err.empty()) {
			printf("Err: %s\n", err.c_str());
		}

		if (!ret) {
			printf("Failed to parse glTF\n");
		}
		else
		{
			printf("Successfully parsed glTF\n");

			std::cout << "meshes(item=" << model.meshes.size() << ")" << std::endl;
			for (size_t i = 0; i < model.meshes.size(); i++) {

				std::cout << "		" << "name     : " << model.meshes[i].name.c_str() << std::endl;
				std::cout << "		" << "primitives (items = " << model.meshes[i].primitives.size() << "): " << std::endl;

				for (size_t k = 0; k < model.meshes[i].primitives.size(); k++) {

					std::cout << "		" << "material : " << model.meshes[i].primitives[k].material << std::endl;
					std::cout << "		" << "indices : " << model.meshes[i].primitives[k].indices << std::endl;

					if (model.meshes[i].primitives[k].mode == TINYGLTF_MODE_POINTS)
						mode = "POINTS";
					else if (model.meshes[i].primitives[k].mode == TINYGLTF_MODE_LINE)
						mode = "LINE";
					else if (model.meshes[i].primitives[k].mode == TINYGLTF_MODE_LINE_LOOP)
						mode = "LINE LOOP";
					else if (model.meshes[i].primitives[k].mode == TINYGLTF_MODE_LINE_STRIP)
						mode = "LINE STRIP";
					else if (model.meshes[i].primitives[k].mode == TINYGLTF_MODE_TRIANGLES)
						mode = "TRIANGLES";
					else if (model.meshes[i].primitives[k].mode == TINYGLTF_MODE_TRIANGLE_STRIP)
						mode = "TRIANGLE STRIP";
					else if (model.meshes[i].primitives[k].mode == TINYGLTF_MODE_TRIANGLE_FAN)
						mode = "TRIANGLE FAN";
					else
						mode = "*ERROR*";

					std::cout << "		" << "mode     : " << mode.c_str() << std::endl;

					std::cout << "		" << "attributes(items=" << model.meshes[i].primitives[k].attributes.size() << ")" << std::endl;

					for (size_t i = 0; i < model.accessors.size(); i++) {
						const tinygltf::Accessor& accessor = model.accessors[i];
						std::cout << "		" << "bufferView   : " << accessor.bufferView << std::endl;
						std::cout << "		" << "byteOffset   : " << accessor.byteOffset << std::endl;
						std::cout << "		" << "count        : " << accessor.count << std::endl;

						if (accessor.type == TINYGLTF_TYPE_SCALAR)
							accType = "SCALAR";
						else if (accessor.type == TINYGLTF_TYPE_VECTOR)
							accType = "VECTOR";
						else if (accessor.type == TINYGLTF_TYPE_VEC2)
							accType = "VEC2";
						else if (accessor.type == TINYGLTF_TYPE_VEC3)
							accType = "VEC3";
						else if (accessor.type == TINYGLTF_TYPE_VEC4)
							accType = "VEC4";
						else if (accessor.type == TINYGLTF_TYPE_MATRIX)
							accType = "MATRIX";
						else if (accessor.type == TINYGLTF_TYPE_MAT2)
							accType = "MAT2";
						else if (accessor.type == TINYGLTF_TYPE_MAT3)
							accType = "MAT3";
						else if (accessor.type == TINYGLTF_TYPE_MAT4)
							accType = "MAT4";
						else
							accType = "*ERROR*";

						std::cout << "		" << "type     : " << accType.c_str() << std::endl;

						if (!accessor.minValues.empty()) {
							std::cout << "		" << "min          : [";
							for (size_t k = 0; k < accessor.minValues.size(); k++) {
								std::cout << accessor.minValues[k]
									<< ((k != accessor.minValues.size() - 1) ? ", " : "");
							}
							std::cout << "]" << std::endl;
						}
						if (!accessor.maxValues.empty()) {
							std::cout << "		" << "max          : [";
							for (size_t k = 0; k < accessor.maxValues.size(); k++) {
								std::cout << accessor.maxValues[k]
									<< ((k != accessor.maxValues.size() - 1) ? ", " : "");
							}
							std::cout << "]" << std::endl;
						}
					}
				}

				std::cout << "bufferViews (items = " << model.bufferViews.size() << ")" << std::endl;

				for (size_t i = 0; i < model.bufferViews.size(); i++) {
					const tinygltf::BufferView& bufferView = model.bufferViews[i];
					std::cout << "		" << "name         : " << bufferView.name << std::endl;
					std::cout << "		" << "buffer       : " << bufferView.buffer << std::endl;
					std::cout << "		" << "byteLength   : " << bufferView.byteLength << std::endl;
					std::cout << "		" << "byteOffset   : " << bufferView.byteOffset << std::endl;
					std::cout << "		" << "byteStride   : " << bufferView.byteStride << std::endl;

					std::cout << "		" << "-------------------------------------\n";

					if (!bufferView.extensions_json_string.empty()) {
						std::cout << "		" << "extensions(JSON string) = "
							<< bufferView.extensions_json_string << "\n";
					}

					if (!bufferView.extras_json_string.empty()) {
						std::cout << "		"
							<< "extras(JSON string) = " << bufferView.extras_json_string
							<< "\n";
					}
				}

			}



		}
	}

	void LoadTextureFromGLTFFile()
	{
		const size_t size = model.images.size();

		textures.resize(size);

		std::cout << "Model Images Size: " << std::to_string(model.images.size()) << std::endl;

		for (size_t i = 0; i < size; i++)
		{
			Image img = model.images[i];

			UploadTextureToGPU(vlk, img, textures[i].memory, textures[i].image, textures[i].imageView);
		}

	}

	void InitializeGeometryBuffer()
	{
		const Primitive& primitive = model.meshes[0].primitives[0];

		const Accessor& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
		const Accessor& normAccessor = model.accessors.at(primitive.attributes.at("NORMAL"));
		const Accessor& uvAccessor = model.accessors.at(primitive.attributes.at("TEXCOORD_0"));
		const Accessor& tanAccessor = model.accessors.at(primitive.attributes.at("TANGENT"));

		const BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
		const BufferView& normBufferView = model.bufferViews[normAccessor.bufferView];
		const BufferView& uvBufferView = model.bufferViews[uvAccessor.bufferView];
		const BufferView& tanBufferView = model.bufferViews[tanAccessor.bufferView];

		const Buffer& posBuffer = model.buffers[posBufferView.buffer];
		const Buffer& normBuffer = model.buffers[normBufferView.buffer];
		const Buffer& uvBuffer = model.buffers[uvBufferView.buffer];
		const Buffer& tanBuffer = model.buffers[tanBufferView.buffer];

		const float* dataPos = reinterpret_cast<const float*>(posBuffer.data.data() + posBufferView.byteOffset + posAccessor.byteOffset);

		const unsigned int dataPosSize = posBufferView.byteLength;

		const Accessor& indexAccessor = model.accessors[primitive.indices];
		const BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
		const Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

		const size_t posSize = static_cast<size_t>(posBufferView.byteLength);
		const size_t normSize = static_cast<size_t>(normBufferView.byteLength);
		const size_t uvSize = static_cast<size_t>(uvBufferView.byteLength);
		const size_t tanSize = static_cast<size_t>(tanBufferView.byteLength);
		const size_t indexSize = static_cast<size_t>(indexBufferView.byteLength);

		const size_t totalSize = posSize + normSize + uvSize + tanSize + indexSize;
		geom.resize(totalSize);

		size_t offset = 0;

		std::memcpy(geom.data() + offset, posBuffer.data.data() + posBufferView.byteOffset + posAccessor.byteOffset, posSize);
		vertexBindingOffsets[0] = static_cast<VkDeviceSize>(offset);
		offset += posSize;

		std::memcpy(geom.data() + offset, normBuffer.data.data() + normBufferView.byteOffset + normAccessor.byteOffset, normSize);
		vertexBindingOffsets[1] = static_cast<VkDeviceSize>(offset);
		offset += normSize;

		std::memcpy(geom.data() + offset, uvBuffer.data.data() + uvBufferView.byteOffset + uvAccessor.byteOffset, uvSize);
		vertexBindingOffsets[2] = static_cast<VkDeviceSize>(offset);
		offset += uvSize;

		std::memcpy(geom.data() + offset, tanBuffer.data.data() + tanBufferView.byteOffset + tanAccessor.byteOffset, tanSize);
		vertexBindingOffsets[3] = static_cast<VkDeviceSize>(offset);
		offset += tanSize;

		std::memcpy(geom.data() + offset, indexBuffer.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset, indexSize);
		indexStartOffset = static_cast<VkDeviceSize>(offset);
		offset += indexSize;

		assert(offset == totalSize);

		CreateGeometryIndex(geom.data(), geom.size());
	}

	void CreateGeometryIndex(const void* data, unsigned int sizeInBytes)
	{
		GvkHelper::create_buffer(physicalDevice, device, sizeInBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&geometryHandle, &geometryData);
		GvkHelper::write_to_buffer(device, geometryData, data, sizeInBytes); // Transfer triangle data to the vertex buffer. (staging would be prefered here)
	}

	void CompileShaders()
	{
		// Intialize runtime shader compiler HLSL -> SPIRV
		shaderc_compiler_t compiler = shaderc_compiler_initialize();
		shaderc_compile_options_t options = CreateCompileOptions();

		CompileVertexShader(compiler, options);
		CompileFragmentShader(compiler, options);

		// Free runtime shader compiler resources
		shaderc_compile_options_release(options);
		shaderc_compiler_release(compiler);
	}

	shaderc_compile_options_t CreateCompileOptions()
	{
		shaderc_compile_options_t retval = shaderc_compile_options_initialize();
		shaderc_compile_options_set_source_language(retval, shaderc_source_language_hlsl);
		shaderc_compile_options_set_invert_y(retval, false);
#ifndef NDEBUG
		shaderc_compile_options_set_generate_debug_info(retval);
#endif
		return retval;
	}

	void CompileVertexShader(const shaderc_compiler_t& compiler, const shaderc_compile_options_t& options)
	{
		std::string vertexShaderSource = ReadFileIntoString("../VertexShader.hlsl");

		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, vertexShaderSource.c_str(), vertexShaderSource.length(),
			shaderc_vertex_shader, "main.vert", "main", options);

		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
		{
			PrintLabeledDebugString("Vertex Shader Errors: \n", shaderc_result_get_error_message(result));
			abort(); //Vertex shader failed to compile! 
			return;
		}

		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &vertexShader);

		shaderc_result_release(result); // done
	}

	void CompileFragmentShader(const shaderc_compiler_t& compiler, const shaderc_compile_options_t& options)
	{
		std::string fragmentShaderSource = ReadFileIntoString("../FragmentShader.hlsl");

		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, fragmentShaderSource.c_str(), fragmentShaderSource.length(),
			shaderc_fragment_shader, "main.frag", "main", options);

		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
		{
			PrintLabeledDebugString("Fragment Shader Errors: \n", shaderc_result_get_error_message(result));
			abort(); //Fragment shader failed to compile! 
			return;
		}

		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &fragmentShader);

		shaderc_result_release(result); // done
	}

	// Create Pipeline & Layout (Thanks Tiny!)
	void InitializeGraphicsPipeline()
	{
		VkPipelineShaderStageCreateInfo stage_create_info[2] = {};

		// Create Stage Info for Vertex Shader
		stage_create_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stage_create_info[0].module = vertexShader;
		stage_create_info[0].pName = "main";

		// Create Stage Info for Fragment Shader
		stage_create_info[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stage_create_info[1].module = fragmentShader;
		stage_create_info[1].pName = "main";


		VkPipelineInputAssemblyStateCreateInfo assembly_create_info = CreateVkPipelineInputAssemblyStateCreateInfo();
		std::array<VkVertexInputBindingDescription, 4> vertex_binding_description{};
		for (int i = 0; i < vertex_binding_description.size(); i++)
		{
			vertex_binding_description[i].binding = i;

			vertex_binding_description[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			vertex_binding_description[i].stride = 0;
		}

		const Primitive& primitive = model.meshes[0].primitives[0];
		const Accessor& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
		const Accessor& normAccessor = model.accessors[primitive.attributes.at("NORMAL")];
		const Accessor& uvAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
		const Accessor& tanAccessor = model.accessors[primitive.attributes.at("TANGENT")];

		auto strideOrCalc = [&](const Accessor& a, const BufferView& bv)->uint32_t {

			if (bv.byteStride > 0)
				return static_cast<uint32_t>(bv.byteStride);

			if (a.type == TINYGLTF_TYPE_VEC4)
				return 4 * sizeof(float);

			if (a.type == TINYGLTF_TYPE_VEC3)
				return 3 * sizeof(float);

			if (a.type == TINYGLTF_TYPE_VEC2)
				return 2 * sizeof(float);

			if (a.type == TINYGLTF_TYPE_SCALAR)
				return sizeof(float);

			return 0u;
			};

		vertex_binding_description[0].stride = strideOrCalc(posAccessor, model.bufferViews[posAccessor.bufferView]);
		vertex_binding_description[1].stride = strideOrCalc(normAccessor, model.bufferViews[normAccessor.bufferView]);
		vertex_binding_description[2].stride = strideOrCalc(uvAccessor, model.bufferViews[uvAccessor.bufferView]);
		vertex_binding_description[3].stride = strideOrCalc(tanAccessor, model.bufferViews[tanAccessor.bufferView]);

		std::array<VkVertexInputAttributeDescription, 4> vertex_attribute_descriptions{};
		vertex_attribute_descriptions[0].binding = 0;
		vertex_attribute_descriptions[0].location = 0;
		vertex_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertex_attribute_descriptions[0].offset = 0;

		vertex_attribute_descriptions[1].binding = 1;
		vertex_attribute_descriptions[1].location = 1;
		vertex_attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertex_attribute_descriptions[1].offset = 0;

		vertex_attribute_descriptions[2].binding = 2;
		vertex_attribute_descriptions[2].location = 2;
		vertex_attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		vertex_attribute_descriptions[2].offset = 0;

		vertex_attribute_descriptions[3].binding = 3;
		vertex_attribute_descriptions[3].location = 3;
		vertex_attribute_descriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		vertex_attribute_descriptions[3].offset = 0;

		VkPipelineVertexInputStateCreateInfo input_vertex_info = CreateVkPipelineVertexInputStateCreateInfo(vertex_binding_description.data(), static_cast<unsigned int>(vertex_binding_description.size()), vertex_attribute_descriptions.data(), static_cast<unsigned int>(vertex_attribute_descriptions.size()));
		VkViewport viewport = CreateViewportFromWindowDimensions();
		VkRect2D scissor = CreateScissorFromWindowDimensions();
		VkPipelineViewportStateCreateInfo viewport_create_info = CreateVkPipelineViewportStateCreateInfo(&viewport, 1, &scissor, 1);
		VkPipelineRasterizationStateCreateInfo rasterization_create_info = CreateVkPipelineRasterizationStateCreateInfo();
		VkPipelineMultisampleStateCreateInfo multisample_create_info = CreateVkPipelineMultisampleStateCreateInfo();
		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = CreateVkPipelineDepthStencilStateCreateInfo();
		VkPipelineColorBlendAttachmentState color_blend_attachment_state = CreateVkPipelineColorBlendAttachmentState();
		VkPipelineColorBlendStateCreateInfo color_blend_create_info = CreateVkPipelineColorBlendStateCreateInfo(&color_blend_attachment_state, 1);

		VkDynamicState dynamic_states[2] =
		{
			// By setting these we do not need to re-create the pipeline on Resize
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamic_create_info = CreateVkPipelineDynamicStateCreateInfo(dynamic_states, 2);

		CreatePipelineLayout();

		// Pipeline State... (FINALLY) 
		VkGraphicsPipelineCreateInfo pipeline_create_info = {};
		pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_create_info.stageCount = 2;
		pipeline_create_info.pStages = stage_create_info;
		pipeline_create_info.pInputAssemblyState = &assembly_create_info;
		pipeline_create_info.pVertexInputState = &input_vertex_info;
		pipeline_create_info.pViewportState = &viewport_create_info;
		pipeline_create_info.pRasterizationState = &rasterization_create_info;
		pipeline_create_info.pMultisampleState = &multisample_create_info;
		pipeline_create_info.pDepthStencilState = &depth_stencil_create_info;
		pipeline_create_info.pColorBlendState = &color_blend_create_info;
		pipeline_create_info.pDynamicState = &dynamic_create_info;
		pipeline_create_info.layout = pipelineLayout;
		pipeline_create_info.renderPass = renderPass;
		pipeline_create_info.subpass = 0;
		pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;

		vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline);
	}

	VkPipelineShaderStageCreateInfo CreateVertexShaderStageCreateInfo()
	{
		VkPipelineShaderStageCreateInfo retval;
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		retval.stage = VK_SHADER_STAGE_VERTEX_BIT;
		retval.module = vertexShader;
		retval.pName = "main";
		return retval;
	}

	VkPipelineInputAssemblyStateCreateInfo CreateVkPipelineInputAssemblyStateCreateInfo()
	{
		VkPipelineInputAssemblyStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		retval.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		retval.primitiveRestartEnable = false;
		return retval;
	}

	VkVertexInputBindingDescription CreateVkVertexInputBindingDescription()
	{
		VkVertexInputBindingDescription retval = {};
		retval.binding = 0;
		retval.stride = sizeof(float) * 3;
		retval.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		return retval;
	}

	VkPipelineVertexInputStateCreateInfo CreateVkPipelineVertexInputStateCreateInfo(
		VkVertexInputBindingDescription* inputBindingDescriptions, unsigned int bindingCount,
		VkVertexInputAttributeDescription* vertexAttributeDescriptions, unsigned int attributeCount)
	{
		VkPipelineVertexInputStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		retval.vertexBindingDescriptionCount = bindingCount;
		retval.pVertexBindingDescriptions = inputBindingDescriptions;
		retval.vertexAttributeDescriptionCount = attributeCount;
		retval.pVertexAttributeDescriptions = vertexAttributeDescriptions;
		return retval;
	}

	VkViewport CreateViewportFromWindowDimensions()
	{
		VkViewport retval = {};
		retval.x = 0;
		retval.y = 0;
		retval.width = static_cast<float>(windowWidth);
		retval.height = static_cast<float>(windowHeight);
		retval.minDepth = 0;
		retval.maxDepth = 1;
		return retval;
	}

	VkRect2D CreateScissorFromWindowDimensions()
	{
		VkRect2D retval = {};
		retval.offset.x = 0;
		retval.offset.y = 0;
		retval.extent.width = windowWidth;
		retval.extent.height = windowHeight;
		return retval;
	}

	VkPipelineViewportStateCreateInfo CreateVkPipelineViewportStateCreateInfo(VkViewport* viewports, unsigned int viewportCount, VkRect2D* scissors, unsigned int scissorCount)
	{
		VkPipelineViewportStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		retval.viewportCount = viewportCount;
		retval.pViewports = viewports;
		retval.scissorCount = scissorCount;
		retval.pScissors = scissors;
		return retval;
	}

	VkPipelineRasterizationStateCreateInfo CreateVkPipelineRasterizationStateCreateInfo()
	{
		VkPipelineRasterizationStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		retval.rasterizerDiscardEnable = VK_FALSE;
		retval.polygonMode = VK_POLYGON_MODE_FILL;
		retval.lineWidth = 1.0f;
		retval.cullMode = VK_CULL_MODE_BACK_BIT;
		retval.frontFace = VK_FRONT_FACE_CLOCKWISE;
		retval.depthClampEnable = VK_FALSE;
		retval.depthBiasEnable = VK_FALSE;
		retval.depthBiasClamp = 0.0f;
		retval.depthBiasConstantFactor = 0.0f;
		retval.depthBiasSlopeFactor = 0.0f;
		return retval;
	}

	VkPipelineMultisampleStateCreateInfo CreateVkPipelineMultisampleStateCreateInfo()
	{
		VkPipelineMultisampleStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		retval.sampleShadingEnable = VK_FALSE;
		retval.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		retval.minSampleShading = 1.0f;
		retval.pSampleMask = VK_NULL_HANDLE;
		retval.alphaToCoverageEnable = VK_FALSE;
		retval.alphaToOneEnable = VK_FALSE;
		return retval;
	}

	VkPipelineDepthStencilStateCreateInfo CreateVkPipelineDepthStencilStateCreateInfo()
	{
		VkPipelineDepthStencilStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		retval.depthTestEnable = VK_TRUE;
		retval.depthWriteEnable = VK_TRUE;
		retval.depthCompareOp = VK_COMPARE_OP_LESS;
		retval.depthBoundsTestEnable = VK_FALSE;
		retval.minDepthBounds = 0.0f;
		retval.maxDepthBounds = 1.0f;
		retval.stencilTestEnable = VK_FALSE;
		return retval;
	}

	VkPipelineColorBlendAttachmentState CreateVkPipelineColorBlendAttachmentState()
	{
		VkPipelineColorBlendAttachmentState retval = {};
		retval.colorWriteMask = 0xF;
		retval.blendEnable = VK_FALSE;
		retval.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		retval.dstColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		retval.colorBlendOp = VK_BLEND_OP_ADD;
		retval.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		retval.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		retval.alphaBlendOp = VK_BLEND_OP_ADD;
		return retval;
	}

	VkPipelineColorBlendStateCreateInfo CreateVkPipelineColorBlendStateCreateInfo(VkPipelineColorBlendAttachmentState* attachments, unsigned int attachmentCount)
	{
		VkPipelineColorBlendStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		retval.logicOpEnable = VK_FALSE;
		retval.logicOp = VK_LOGIC_OP_COPY;
		retval.attachmentCount = attachmentCount;
		retval.pAttachments = attachments;
		retval.blendConstants[0] = 0.0f;
		retval.blendConstants[1] = 0.0f;
		retval.blendConstants[2] = 0.0f;
		retval.blendConstants[3] = 0.0f;
		return retval;
	}

	VkPipelineDynamicStateCreateInfo CreateVkPipelineDynamicStateCreateInfo(VkDynamicState* dynamicStates, unsigned int dynamicStateCount)
	{
		VkPipelineDynamicStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		retval.dynamicStateCount = dynamicStateCount;
		retval.pDynamicStates = dynamicStates;
		return retval;
	}

	void CreatePipelineLayout()
	{

		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 2;

		VkDescriptorSetLayout descriptorLayouts[2] = { descriptorSetLayout, textureDescriptorSetLayout };

		pipeline_layout_create_info.pSetLayouts = descriptorLayouts;
		pipeline_layout_create_info.pushConstantRangeCount = 0;
		pipeline_layout_create_info.pPushConstantRanges = nullptr;

		vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &pipelineLayout);
	}

	void UniformBufferInit()
	{
		uint32_t imageNum;
		vlk.GetSwapchainImageCount(imageNum);

		uniformBuffers.resize(imageNum);
		uniformBuffersData.resize(imageNum);

		for (unsigned int i = 0; i < imageNum; i++)
		{
			GvkHelper::create_buffer(
				physicalDevice,
				device,
				sizeof(MATRIX_VARS),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				&uniformBuffers[i],
				&uniformBuffersData[i]
			);

			GvkHelper::write_to_buffer(device, uniformBuffersData[i], &matrixVars, sizeof(MATRIX_VARS));
		}
	}

	void StorageBufferInit()
	{
		uint32_t imageNum;
		vlk.GetSwapchainImageCount(imageNum);

		storageBuffers.resize(imageNum);
		storageBuffersData.resize(imageNum);

		for (unsigned int i = 0; i < imageNum; i++)
		{
			GvkHelper::create_buffer(
				physicalDevice,
				device,
				geom.size(),
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				&storageBuffers[i],
				&storageBuffersData[i]
			);

			GvkHelper::write_to_buffer(device, storageBuffersData[i], geom.data(), geom.size());
		}
	}

	void DescriptorPoolInit()
	{
		std::array<VkDescriptorPoolSize, 3> poolSizes{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = uniformBuffers.size();

		poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSizes[1].descriptorCount = storageBuffers.size();

		poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[2].descriptorCount = textures.size();

		for (size_t i = 0; i < poolSizes.size(); i++)
		{
			std::cout << "Pool [" << i << "]" << std::endl;
			std::cout << "Pool Descriptor Count: " << poolSizes[i].descriptorCount << std::endl;
		}

		VkDescriptorPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = poolSizes.size();
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = uniformBuffers.size() + 1;

		vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
	}

	void LinkDescriptorSetToUniformBuffer()
	{
		for (int i = 0; i < uniformBuffersData.size(); i++)
		{
			VkDescriptorBufferInfo uniformBufferInfo = {};
			uniformBufferInfo.buffer = uniformBuffers[i];
			uniformBufferInfo.offset = 0;
			uniformBufferInfo.range = sizeof(MATRIX_VARS);

			VkDescriptorBufferInfo storageBufferInfo = {};
			storageBufferInfo.buffer = storageBuffers[i];
			storageBufferInfo.offset = 0;
			storageBufferInfo.range = geom.size();

			VkWriteDescriptorSet descriptorWrite[2] = {};
			descriptorWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite[0].dstSet = descriptorSet[i];
			descriptorWrite[0].dstBinding = 0;
			descriptorWrite[0].dstArrayElement = 0;
			descriptorWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrite[0].descriptorCount = 1;
			descriptorWrite[0].pBufferInfo = &uniformBufferInfo;
			descriptorWrite[0].pImageInfo = nullptr;
			descriptorWrite[0].pTexelBufferView = nullptr;

			descriptorWrite[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite[1].dstSet = descriptorSet[i];
			descriptorWrite[1].dstBinding = 1;
			descriptorWrite[1].dstArrayElement = 0;
			descriptorWrite[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			descriptorWrite[1].descriptorCount = 1;
			descriptorWrite[1].pBufferInfo = &storageBufferInfo;
			descriptorWrite[1].pImageInfo = nullptr;
			descriptorWrite[1].pTexelBufferView = nullptr;

			vkUpdateDescriptorSets(device, 2, &descriptorWrite[0], 0, nullptr);
		}

		CreateSampler(vlk, sampleTexture);

		std::vector<VkDescriptorImageInfo> imageInfo = {};
		std::vector<VkDescriptorBufferInfo> descImageBufferInfo = {};

		for (int i = 0; i < textures.size(); i++)
		{
			VkDescriptorBufferInfo textureBufferInfo = {};
			textureBufferInfo.buffer = textures[i].buffer;
			textureBufferInfo.offset = 0;
			textureBufferInfo.range = sizeof(textures);
			descImageBufferInfo.push_back(textureBufferInfo);

			VkDescriptorImageInfo imageBufferInfo = {};
			imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageBufferInfo.imageView = textures[i].imageView;
			imageBufferInfo.sampler = sampleTexture;
			imageInfo.push_back(imageBufferInfo);
		}

		VkWriteDescriptorSet textureDescriptorWrite = {};
		textureDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		textureDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		textureDescriptorWrite.descriptorCount = textures.size();
		textureDescriptorWrite.dstSet = textureDescriptorSet;
		textureDescriptorWrite.pBufferInfo = descImageBufferInfo.data();
		textureDescriptorWrite.pImageInfo = nullptr;
		textureDescriptorWrite.pNext = nullptr;
		textureDescriptorWrite.pTexelBufferView = nullptr;
		textureDescriptorWrite.pImageInfo = imageInfo.data();

		vkUpdateDescriptorSets(device, 1, &textureDescriptorWrite, 0, nullptr);
	}

	void DescriptorSetInit()
	{
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &descriptorSetLayout;

		descriptorSet.resize(uniformBuffers.size());

		for (int i = 0; i < uniformBuffersData.size(); i++)
		{
			vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet[i]);
		}
		
		const uint32_t textureSize = textures.size();

		VkDescriptorSetVariableDescriptorCountAllocateInfoEXT allocateTextureInfoEXT = {};
		allocateTextureInfoEXT.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
		allocateTextureInfoEXT.descriptorSetCount = 1;
		allocateTextureInfoEXT.pDescriptorCounts = &textureSize;
		allocateTextureInfoEXT.pNext = nullptr;

		VkDescriptorSetAllocateInfo descriptorTextureAllocateInfo = {};
		descriptorTextureAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorTextureAllocateInfo.descriptorPool = descriptorPool;
		descriptorTextureAllocateInfo.descriptorSetCount = 1;
		descriptorTextureAllocateInfo.pSetLayouts = &textureDescriptorSetLayout;
		descriptorTextureAllocateInfo.pNext = &allocateTextureInfoEXT;

		vkAllocateDescriptorSets(device, &descriptorTextureAllocateInfo, &textureDescriptorSet);
	}

	void DescriptorLayoutInit()
	{
		VkDescriptorSetLayoutBinding layoutBindings[2] = {};
		layoutBindings[0].binding = 0;
		layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		layoutBindings[0].descriptorCount = 1;
		layoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		layoutBindings[0].pImmutableSamplers = nullptr;

		layoutBindings[1].binding = 1;
		layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		layoutBindings[1].descriptorCount = 1;
		layoutBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		layoutBindings[1].pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 2;
		layoutInfo.pBindings = &layoutBindings[0];

		vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);

		VkDescriptorSetLayoutBinding textureBindings = {};
		textureBindings.binding = 0;
		textureBindings.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		textureBindings.descriptorCount = textures.size();
		textureBindings.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		textureBindings.pImmutableSamplers = nullptr;

		VkDescriptorBindingFlagsEXT bindingTextureFlag = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT;

		VkDescriptorSetLayoutBindingFlagsCreateInfoEXT textureFlag = {};
		textureFlag.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
		textureFlag.bindingCount = 1;
		textureFlag.pBindingFlags = &bindingTextureFlag;

		VkDescriptorSetLayoutCreateInfo textureLayoutInfo = {};
		textureLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		textureLayoutInfo.bindingCount = 1;
		textureLayoutInfo.pBindings = &textureBindings;
		textureLayoutInfo.pNext = &textureFlag;

		vkCreateDescriptorSetLayout(device, &textureLayoutInfo, nullptr, &textureDescriptorSetLayout);
	}

	void BindShutdownCallback()
	{
		// GVulkanSurface will inform us when to release any allocated resources
		shutdown.Create(vlk, [&]() {
			if (+shutdown.Find(GW::GRAPHICS::GVulkanSurface::Events::RELEASE_RESOURCES, true)) {
				CleanUp(); // unlike D3D we must be careful about destroy timing
			}
			});
	}


	void ViewMatrixInit()
	{
		uint32_t image;
		vlk.GetSwapchainCurrentImage(image);

		GW::MATH::GVECTORF eye = { 0.25f, -0.125f, -0.25f, 1.f };
		GW::MATH::GVECTORF at = { 0.0f, -0.5f,  0.0f };
		GW::MATH::GVECTORF upVec = { 0.0f, 1.0f,  0.0f };

		gMatrixProxy.LookAtLHF(eye, at, upVec, viewMatrix);
	}

	void UpdateUniformBufferForImage(uint32_t imageIndex)
	{
		if (imageIndex < uniformBuffersData.size()) {
			GvkHelper::write_to_buffer(device, uniformBuffersData[imageIndex], &matrixVars, sizeof(MATRIX_VARS));
		}
	}

	void ProjectionMatrixInit()
	{
		float aspectRatio;
		vlk.GetAspectRatio(aspectRatio);

		float fov = G_DEGREE_TO_RADIAN_F(65.0f);
		float nearPlane = 0.1f;
		float farPlane = 100.0f;

		gMatrixProxy.ProjectionVulkanLHF(fov, aspectRatio, nearPlane, farPlane, projMatrix);
	}

public:
	void Render()
	{
		VkCommandBuffer commandBuffer = GetCurrentCommandBuffer();

		viewMatrix = FreeLookCamera(win, viewMatrix);
		matrixVars.viewMatrix = viewMatrix;

		uint32_t getImageNum;
		vlk.GetSwapchainCurrentImage(getImageNum);

		UpdateUniformBufferForImage(getImageNum);

		SetUpPipeline(commandBuffer);

		Accessor& indexAccessor = model.accessors[model.meshes[0].primitives[0].indices];

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet[getImageNum], 0, nullptr);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &textureDescriptorSet, 0, nullptr);

		vkCmdDrawIndexed(commandBuffer, indexAccessor.count, 1, 0, 0, 0);
	}

private:
	VkCommandBuffer GetCurrentCommandBuffer()
	{
		unsigned int currentBuffer;
		vlk.GetSwapchainCurrentFrame(currentBuffer);

		VkCommandBuffer commandBuffer;
		vlk.GetCommandBuffer(currentBuffer, (void**)&commandBuffer);
		return commandBuffer;
	}

	void SetUpPipeline(VkCommandBuffer& commandBuffer)
	{
		UpdateWindowDimensions();
		SetViewport(commandBuffer);
		SetScissor(commandBuffer);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		VertexBufferBind(commandBuffer);
		IndexBufferBind(commandBuffer);
	}

	void SetViewport(const VkCommandBuffer& commandBuffer)
	{
		VkViewport viewport = CreateViewportFromWindowDimensions();
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	}

	void SetScissor(const VkCommandBuffer& commandBuffer)
	{
		VkRect2D scissor = CreateScissorFromWindowDimensions();
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	void VertexBufferBind(VkCommandBuffer& commandBuffer)
	{
		const Primitive& primitive = model.meshes[0].primitives[0];

		VkDeviceSize offsets[] = { vertexBindingOffsets[0], vertexBindingOffsets[1], vertexBindingOffsets[2], vertexBindingOffsets[3] };
		VkBuffer buffers[4] = { geometryHandle, geometryHandle, geometryHandle, geometryHandle };

		vkCmdBindVertexBuffers(commandBuffer, 0, 4, buffers, offsets);
	}

	void IndexBufferBind(VkCommandBuffer& commandBuffer)
	{
		const Primitive& primitive = model.meshes[0].primitives[0];
		const Accessor& indiceAccess = model.accessors[primitive.indices];

		VkIndexType indexComponent = VK_INDEX_TYPE_UINT16;

		if (indiceAccess.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
			indexComponent = VK_INDEX_TYPE_UINT32;
		else if (indiceAccess.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
			indexComponent = VK_INDEX_TYPE_UINT16;

		vkCmdBindIndexBuffer(commandBuffer, geometryHandle, indexStartOffset, indexComponent);
	}


	//Cleanup callback function (passed to VKSurface, will be called when the pipeline shuts down)
	void CleanUp()
	{
		// wait till everything has completed
		vkDeviceWaitIdle(device);

		// Release allocated buffers, shaders & pipeline
		vkDestroyBuffer(device, geometryHandle, nullptr);
		vkFreeMemory(device, geometryData, nullptr);

		for (size_t i = 0; i < uniformBuffers.size(); i++)
		{
			vkDestroyBuffer(device, uniformBuffers[i], nullptr);
			vkFreeMemory(device, uniformBuffersData[i], nullptr);
		}

		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, textureDescriptorSetLayout, nullptr);

		for (size_t i = 0; i < storageBuffers.size(); i++)
		{
			vkDestroyBuffer(device, storageBuffers[i], nullptr);
			vkFreeMemory(device, storageBuffersData[i], nullptr);
		}

		vkDestroyShaderModule(device, vertexShader, nullptr);
		vkDestroyShaderModule(device, fragmentShader, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);

		for (size_t i = 0; i < textures.size(); i++)
		{

			vkDestroyBuffer(device, textures[i].buffer, nullptr);
			vkFreeMemory(device, textures[i].memory, nullptr);
			vkDestroyImageView(device, textures[i].imageView, nullptr);
			vkDestroyImage(device, textures[i].image, nullptr);
		}

		vkDestroySampler(device, sampleTexture, nullptr);
	}
};
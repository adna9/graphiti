#pragma once

#include <raindance/Core/Headers.hh>
#include <raindance/Core/Buffer.hh>
#include <raindance/Core/Transformation.hh>
#include <raindance/Core/Camera/Camera.hh>
#include <raindance/Core/Batch.hh>

class WorldMap : public Document::Node
{
public:

	struct Node
	{
		glm::vec2 Location;
		glm::vec4 Color;
		float Size;
	};

	WorldMap(Document::Node* parent = NULL)
	: Document::Node(parent)
	{       
		background().loadImage("WorldMap/miller", FS::BinaryFile("Assets/Earth/miller-bw-cropped.png"));
	
		FS::TextFile vert("Assets/WorldMap/node.vert");
    	FS::TextFile geom("Assets/WorldMap/node.geom");
    	FS::TextFile frag("Assets/WorldMap/node.frag");
    	m_Shader = ResourceManager::getInstance().loadShader("WorldMap/node",
    		vert.content(),
    		frag.content(),
    		geom.content());
    	m_Shader->dump();

		Node node;
		node.Size = 5.0;

		node.Color = glm::vec4(LOVE_RED, 0.75);
		node.Location = glm::vec2(48.8567, 2.3508);
		m_VertexBuffer.push(&node, sizeof(Node));

		node.Color = glm::vec4(SKY_BLUE, 0.75);
		node.Location = glm::vec2(37.783333, -122.416667);
		m_VertexBuffer.push(&node, sizeof(Node));

    	m_VertexBuffer.describe("a_Location", GL_FLOAT, 2);
        m_VertexBuffer.describe("a_Color",    GL_FLOAT, 4);
        m_VertexBuffer.describe("a_Size",     GL_FLOAT, 1);
    	m_VertexBuffer.generate(Buffer::DYNAMIC);

    	auto drawcall = new DrawArrays();
    	drawcall->shader(m_Shader);
    	drawcall->add(&m_VertexBuffer);

    	m_Batch.add(drawcall);
	}

	virtual ~WorldMap()
	{
	}

	void draw(Context* context) override
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_DST_ALPHA);

		m_Camera.setOrthographicProjection(0.0, this->content().getWidth(), 0.0, this->content().getHeight(), -10, 10);

		m_Shader->use();
		m_Shader->uniform("u_ProjectionMatrix").set(m_Camera.getProjectionMatrix());
		m_Shader->uniform("u_Dimension").set(glm::vec2(this->content().getWidth(), this->content().getHeight()));
		m_Batch.execute(context);
	}

	void idle(Context* context) override
	{
		(void) context;
	}

	void request(const Variables& input, Variables& output) override
	{
		//LOG("[WORLDMAP] Request received!\n");
		//input.dump();

		std::string function;
		if (!input.get("function", function))
			return;

		bool error = false;

		if (function == "add")
		{
			float latitude = 0;
			float longitude = 0;
			float size = 5.0;
			glm::vec4 color = glm::vec4(WHITE, 0.75);

			error &= input.get("latitude", &latitude);
			error &= input.get("longitude", &longitude);
			error &= input.get("color.r", &color.r);
			error &= input.get("color.g", &color.g);
			error &= input.get("color.b", &color.b);
			error &= input.get("color.a", &color.a);
			error &= input.get("size", &size);

			Node node;
			node.Size = size;
			node.Color = color;
			node.Location = glm::vec2(latitude, longitude);
			m_VertexBuffer.push(&node, sizeof(Node));
			m_VertexBuffer.update();
		}

		auto var = new IntVariable();
		var->set(error ? -1 : 1);
		output.set("error", var);
	}

private:
	Batch m_Batch;
	Camera m_Camera;
	Shader::Program* m_Shader;
	Buffer m_VertexBuffer;
};
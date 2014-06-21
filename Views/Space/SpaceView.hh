#pragma once

#include <raindance/Core/Text.hh>
#include <raindance/Core/Icon.hh>
#include <raindance/Core/Intersection.hh>
#include <raindance/Core/Primitives/Sphere.hh>
#include <raindance/Core/Primitives/Line.hh>
#include <raindance/Core/Camera/Camera.hh>
#include <raindance/Core/Camera/Frustrum.hh>
#include <raindance/Core/Transformation.hh>
#include <raindance/Core/Variables.hh>
#include <raindance/Core/Scene.hh>
#include <raindance/Core/Physics.hh>
#include <raindance/Core/Environment.hh>
#include <raindance/Core/Bezier.hh>
#include <raindance/Core/Octree.hh>

#include "Entities/MVC.hh"

#include "Entities/Graph/GraphModel.hh"
#include "Entities/Graph/GraphMessages.hh"

#include "Views/Space/SpaceModel.hh"
#include "Views/Space/SpaceResources.hh"

typedef TranslationMap<SpaceNode::ID, Node::ID> NodeTranslationMap;
typedef TranslationMap<SpaceLink::ID, Link::ID> LinkTranslationMap;
#include "Views/Space/SpaceForces.hh"

#include "Pack.hh"

class SpaceView : public GraphView
{
public:

    enum PhysicsMode { PLAY, PAUSE };

    SpaceView()
    {
        LOG("[SPACEVIEW] Creating space view ...\n");

        m_WindowWidth = glutGet(GLUT_WINDOW_WIDTH);
        m_WindowHeight = glutGet(GLUT_WINDOW_HEIGHT);

        m_Camera.setPerspectiveProjection(60.0f, (float)m_WindowWidth / (float)m_WindowHeight, 0.1f, 1024.0f);
        m_Camera.lookAt(glm::vec3(0, 0, -5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        m_CameraAnimation = false;

        g_SpaceResources = new SpaceResources();

        m_GraphEntity = NULL;

        m_Octree = NULL;

        m_RayPacket = new RayPacket();

        m_PhysicsMode = PAUSE;
        m_LastUpdateTime = 0;
        m_Iterations = 0;
        m_Temperature = 0.2f;
    }

	virtual ~SpaceView()
	{
		delete g_SpaceResources;
		delete m_RayPacket;
	}

	virtual const char* name() const { return "space"; }

	virtual bool bind(Entity* entity)
	{
	    if (entity->type() != Entity::GRAPH)
	    {
	        LOG("[SPACE] Couldn't bind entity to view : Wrong entity type!\n");
	        return false;
	    }

        m_GraphEntity = static_cast<GraphEntity*>(entity);
        m_LinkAttractionForce.bind(m_GraphEntity->model(), &m_NodeMap);
        m_NodeRepulsionForce.bind(m_GraphEntity->model(), &m_NodeMap);

        m_GraphEntity->views().push_back(this);
        m_GraphEntity->listeners().push_back(this);

        g_SpaceResources->Model = m_GraphEntity->model();

        return true;
	}

	void applyDegreeTint()
	{
		std::vector<unsigned int> nodeDegrees;
		nodeDegrees.resize(m_SpaceNodes.size(), 0);
		unsigned int maxDegree = 0;

		Scene::NodeVector::iterator itl;
		for (itl = m_SpaceLinks.begin(); itl != m_SpaceLinks.end(); ++itl)
		{
			if (*itl == NULL)
				continue;

			SpaceLink* link = static_cast<SpaceLink*>((*itl)->getDrawable());

			SpaceNode::ID node1 = link->getNode1();
			SpaceNode::ID node2 = link->getNode2();

			nodeDegrees[node1]++;
			nodeDegrees[node2]++;

			if (nodeDegrees[node1] > maxDegree)
				maxDegree = nodeDegrees[node1];
			if (nodeDegrees[node2] > maxDegree)
				maxDegree = nodeDegrees[node2];
		}

		for (unsigned int n = 0; n < m_SpaceNodes.size(); n++)
		{
			if (m_SpaceNodes[n] == NULL)
				continue;

			float tint = maxDegree == 0 ? 1.0 : 0.3 + 0.7 * (float) nodeDegrees[n] / (float) maxDegree;

			SpaceNode* node = static_cast<SpaceNode*>(m_SpaceNodes[n]->getDrawable());
			node->setColor(tint * node->getColor());
		}
	}

	virtual IVariable* getNodeAttribute(Node::ID uid, std::string& name)
	{
		SpaceNode::ID id = m_NodeMap.getLocalID(uid);

		if (name == "position")
		{
			Vec3Variable* variable = new Vec3Variable();
			variable->set(m_SpaceNodes[id]->getPosition());
			return variable;
		}
		else if (name == "color")
		{
			Vec4Variable* variable = new Vec4Variable();
			variable->set(static_cast<SpaceNode*>(m_SpaceNodes[id]->getDrawable())->getColor());
			return variable;
		}
		else if (name == "locked")
		{
			BooleanVariable* variable = new BooleanVariable();
			variable->set(m_SpaceNodes[id]->isPositionLocked());
			return variable;
		}
		else if (name == "activity")
		{
			FloatVariable* variable = new FloatVariable();
			variable->set(static_cast<SpaceNode*>(m_SpaceNodes[id]->getDrawable())->getActivity());
			return variable;
		}

		return NULL;
	}

	virtual IVariable* getLinkAttribute(Link::ID uid, std::string& name)
	{
		SpaceLink::ID id = m_LinkMap.getLocalID(uid);

		if (name == "activity")
		{
			FloatVariable* variable = new FloatVariable();
			variable->set(static_cast<SpaceLink*>(m_SpaceLinks[id]->getDrawable())->getActivity());
			return variable;
		}
		else if (name == "color1")
		{
		    Vec4Variable* variable = new Vec4Variable();
		    variable->set(static_cast<SpaceLink*>(m_SpaceLinks[id]->getDrawable())->getColor(0));
		    return variable;
		}
		else if (name == "color2")
		{
            Vec4Variable* variable = new Vec4Variable();
            variable->set(static_cast<SpaceLink*>(m_SpaceLinks[id]->getDrawable())->getColor(1));
            return variable;
		}

		return NULL;
	}

	inline float computeNodeSize(const Node& node) const { return node.data().Weight * g_SpaceResources->NodeIconSize; }

	void draw()
	{
	    const glm::vec4 bgcolor = glm::vec4(BLACK, 1.0);
        glClearColor(bgcolor.r, bgcolor.g, bgcolor.b, bgcolor.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        g_SpaceResources->m_Wallpaper->draw(context());

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_DST_ALPHA);

        // NOTE : In the future, we want to disable the depth test, and render the image by layers.
        // However this means we need to sort the nodes by distance to the eye and use the Painter's algorithm

		Transformation transformation;

		// Draw Edges
		if (g_SpaceResources->ShowEdges || g_SpaceResources->ShowEdgeActivity)
		{
#ifndef EMSCRIPTEN
			// NOTE : Not supported by WebGL
			glEnable(GL_LINE_SMOOTH);
			glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
#endif
			m_SpaceLinks.draw(context(), m_Camera.getProjectionMatrix(), m_Camera.getViewMatrix(), transformation.state());
		}

		// Draw Nodes
		{
            if (m_Octree == NULL)
            {
                m_SpaceNodes.draw(context(), m_Camera.getProjectionMatrix(), m_Camera.getViewMatrix(), transformation.state());
            }
            else
            {
                static std::vector<OctreeElement*> elements;
                elements.clear();

                Frustrum frustrum(m_Camera.getViewProjectionMatrix());

                // m_Octree->getElementsInsideBox(glm::vec3(-50, -50, -50), glm::vec3(50, 50, 50), elements);
                m_Octree->findElementsInsideFrustrum(frustrum, elements);
                // LOG("Visible elements : %lu\n", elements.size());
                for (auto e : elements)
                {
                    Scene::Node* node = static_cast<Scene::OctreeNode*>(e)->getNode();
                    if (node != NULL)
                        node->draw(context(), m_Camera.getProjectionMatrix(), m_Camera.getViewMatrix(), transformation.state());
                }

                // m_Octree->draw(context(), m_Camera.getProjectionMatrix(), m_Camera.getViewMatrix(), transformation.state());
            }

            std::set<Node::ID>::iterator iti;
            for (iti = model()->selectedNodes_begin(); iti != model()->selectedNodes_end(); ++iti)
            {
                SpaceNode::ID selectedID = m_NodeMap.getLocalID(*iti);
                SpaceNode* selectedNode = static_cast<SpaceNode*>(m_SpaceNodes[selectedID]->getDrawable());

                float iconSize = 2.0f * selectedNode->getSize() * g_SpaceResources->NodeIconSize;
                glm::mat4 modelView = m_Camera.getViewMatrix() * glm::translate(transformation.state(), m_SpaceNodes[selectedID]->getPosition());
                modelView = glm::scale(Geometry::billboard(modelView), glm::vec3(iconSize, iconSize, iconSize));
                g_SpaceResources->NodeTargetIcon->draw(context(), m_Camera.getProjectionMatrix() * modelView, glm::vec4(1.0, 1.0, 1.0, 1.0), 0);
            }
		}

		// Draw spheres
		if (g_SpaceResources->ShowSpheres)
			m_SpaceSpheres.draw(context(), m_Camera.getProjectionMatrix(), m_Camera.getViewMatrix(), transformation.state());

		if (g_SpaceResources->ShowDebug)
			m_RayPacket->draw(context(), m_Camera.getProjectionMatrix() * m_Camera.getViewMatrix() * transformation.state());

	}

	void computeBoundingSphere(Sphere& sphere, glm::vec3* center, float* radius)
	{
		glm::vec3 barycenter = glm::vec3(0, 0, 0);
		float r = 1.0;

		for (unsigned int n = 0; n < sphere.data().Nodes.size(); n++)
		{
			barycenter = barycenter + m_SpaceNodes[sphere.data().Nodes[n]]->getPosition() / (float) sphere.data().Nodes.size();
		}

		for (unsigned int n = 0; n < sphere.data().Nodes.size(); n++)
		{
			float length = glm::length(barycenter - m_SpaceNodes[sphere.data().Nodes[n]]->getPosition());
			if (length > r)
				r = length;
		}

		*radius = r;
		*center = barycenter;
	}

	SpaceNode::ID pushNodeVertexAround(Node::ID uid, const char* label, glm::vec3 position, float radius)
	{
		float rnd1 = (float) rand();
		float rnd2 = (float) rand();
		float rnd3 = 0.8f + ((float) rand() / RAND_MAX) / 5;

		Scene::Node* node = new Scene::Node(NULL, false);
		unsigned long vid = m_SpaceNodes.add(node);

		SpaceNode* spaceNode = new SpaceNode(vid, label);
		node->setDrawable(spaceNode, true);
		node->setPosition(position + glm::vec3(radius * rnd3 * sin(rnd1) * cos(rnd2),
											   radius * rnd3 * cos(rnd1),
											   radius * rnd3 * sin(rnd1) * sin(rnd2)));

		m_NodeMap.addRemoteID(uid, vid);

		return vid;
	}

	void notify(IMessage* message)
	{
		IMessage::Type type = message->type();

		if (type == IMessage::WIDGET)
		{
			WidgetMessage* msg = static_cast<WidgetMessage*>(message);

			if (msg->Message == "play")
			{
				m_Iterations = 1;
				m_PhysicsMode = PLAY;
			}
			else if (msg->Message == "pause")
			{
				m_PhysicsMode = PAUSE;
			}
			else if (msg->Message == "show node all")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::ALL;
				g_SpaceResources->ShowNodeLabels = true;
			}
			else if (msg->Message == "show node colors+labels")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::COLORS;
				g_SpaceResources->ShowNodeLabels = true;
			}
			else if (msg->Message == "show node marks+labels")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::MARKS;
				g_SpaceResources->ShowNodeLabels = true;
			}
			else if (msg->Message == "show node colors+marks")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::ALL;
				g_SpaceResources->ShowNodeLabels = false;
			}
			else if (msg->Message == "show node colors")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::COLORS;
				g_SpaceResources->ShowNodeLabels = false;
			}
			else if (msg->Message == "show node marks")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::MARKS;
				g_SpaceResources->ShowNodeLabels = false;
			}
			else if (msg->Message == "show node labels")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::NONE;
				g_SpaceResources->ShowNodeLabels = true;
			}
			else if (msg->Message == "show node none")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::NONE;
				g_SpaceResources->ShowNodeLabels = false;
			}

			else if (msg->Message == "show edges")
				g_SpaceResources->ShowEdges = true;
			else if (msg->Message == "hide edges")
				g_SpaceResources->ShowEdges = false;

			else if (msg->Message == "show spheres")
				g_SpaceResources->ShowSpheres = true;
			else if (msg->Message == "hide spheres")
				g_SpaceResources->ShowSpheres = false;

			else if (msg->Message == "show debug")
				g_SpaceResources->ShowDebug = true;
			else if (msg->Message == "hide debug")
				g_SpaceResources->ShowDebug = false;
		}
	}

	bool pickNode(int x, int y, SpaceNode::ID* id)
	{
		Ray ray = m_Camera.createRay(x, y);

		// Debug rays
		if (g_SpaceResources->ShowDebug)
			m_RayPacket->add(ray);

		Intersection::Hit hit;
		bool found = false;
		glm::vec3 position;

		float min_distance = std::numeric_limits<float>::max();

		for (unsigned int i = 0; i < m_SpaceNodes.size(); i++)
		{
			if (m_SpaceNodes[i] == NULL)
				continue;

			SpaceNode* spaceNode = static_cast<SpaceNode*>(m_SpaceNodes[i]->getDrawable());

            if (spaceNode->getLOD() == 0.0)
                continue;

            position = m_SpaceNodes[i]->getPosition();

			float pickRadius = g_SpaceResources->NodeIconSize * spaceNode->getSize() / 2.0f;

			if (Intersection::RaySphere(ray, position, pickRadius, hit))
			{
				if (id == NULL)
					return true;

				if (hit.distance < min_distance)
				{
					*id = i;
					min_distance = hit.distance;
				}

				found = true;
			}
		}

		return found;
	}

	void idle()
	{
		updateNodes();
		updateLinks();
		updateSpheres();

		if (m_CameraAnimation)
		{
			float time = context()->sequencer().track("animation")->clock().seconds();
			glm::vec3 pos;
			float radius = m_DustAttractor.getRadius() * (0.4 + 0.25 * cos(time / 30.f));
			pos.x = radius * cos(time / 10.0f);
			pos.y = radius * cos(time / 50.0f);
			pos.z = radius * sin(time / 10.0f);
			m_Camera.lookAt(pos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0));
		}
	}

	void updateLinks()
	{
	    if (g_SpaceResources->ShowEdges || g_SpaceResources->ShowEdgeActivity)
	    {
            Scene::NodeVector::iterator it;
            for (it = m_SpaceLinks.begin(); it != m_SpaceLinks.end(); ++it)
                if (*it != NULL)
                    static_cast<SpaceLink*>((*it)->getDrawable())->update();
	    }
	}

	void updateSpheres()
	{
		glm::vec3 position;
		float radius;

		std::vector<Sphere>::iterator its;
		for (its = model()->spheres_begin(); its != model()->spheres_end(); ++its)
		{
			if (m_SpaceSpheres[its->id()] != NULL)
			{
				computeBoundingSphere(*its, &position, &radius);
				m_SpaceSpheres[its->id()]->setPosition(position);
				static_cast<SpaceSphere*>(m_SpaceSpheres[its->id()]->getDrawable())->setRadius(radius);
			}
		}
	}

	void updateNodes()
	{
		const unsigned int c_MaxIterations = 100;
		bool c_Loop = true;

		if (m_Iterations < 1 || (!c_Loop  && m_Iterations > c_MaxIterations) || m_PhysicsMode == PAUSE)
			return;
		else if (m_Iterations == 1)
			m_LastUpdateTime = m_Clock.milliseconds();

		// m_SpaceNodes.setSpeed(20.0f / (float)m_Iterations);
		m_SpaceNodes.setSpeed(m_Temperature);

		// Reset node directions
		Scene::NodeVector::iterator itn;
		for (itn = m_SpaceNodes.begin(); itn != m_SpaceNodes.end(); ++itn)
			if (*itn != NULL)
				(*itn)->setDirection(glm::vec3(0, 0, 0), false);

		// Calculate graph forces
		m_NodeRepulsionForce.apply(m_SpaceNodes);
		m_LinkAttractionForce.apply(m_SpaceNodes);
		m_DustAttractor.apply(m_SpaceNodes);
		// m_GravitationForce.apply(m_SpaceNodes);

		// Compute Sphere Attraction Force
		// TODO : Find a nice way to factorize this code in its own Force class
		/*
		{
			const float c_MinNodeDistance = 10.0f;
			const float volume = 20 * 20 * 20; // NOTE : Graph should fit in this cube
			float k = pow(volume / m_GraphModel->countNodes(), 1.0 / 3.0);

			glm::vec3 pos1, dir1;
			glm::vec3 barycenter;
			float radius;

			std::vector<Sphere>::iterator its;
			std::vector<Node::ID>::iterator itid;
			for (its = m_GraphModel->spheres_begin(); its != m_GraphModel->spheres_end(); ++its)
			{
				computeBoundingSphere(*its, &barycenter, &radius);

				for (itid = its->data().Nodes.begin(); itid != its->data().Nodes.end(); ++itid)
				{
					pos1 = m_SpaceNodes[*itid]->getPosition();
					dir1 = m_SpaceNodes[*itid]->getDirection();

					glm::vec3 dir = pos1 - barycenter;
					float d = glm::length(dir);
					if (d < c_MinNodeDistance)
						continue;

					// Attractive Force : fa(x) = x * x / k
					dir1 = dir1 - 3.0f * (dir / d) * (d * d / k);

					m_SpaceNodes[*itid]->setDirection(dir1, false);
				}
			}
		}
		*/
		m_SpaceNodes.normalizeDirections();
		m_SpaceNodes.randomizeDirections();
		m_SpaceNodes.update();

		if (!c_Loop)
		{
			Timecode mean = (m_Clock.milliseconds() - m_LastUpdateTime) / m_Iterations;
			LOG("[SPACE] Iteration %u/%u, Mean Update Time : %lu ms, Estimated Remaining Time : %lu ms\n", m_Iterations, c_MaxIterations, mean, (c_MaxIterations - m_Iterations) * mean);
		}

		m_Iterations++;
	}

	inline Camera* camera() { return &m_Camera; }

    inline void setNodeSize(float size) { g_SpaceResources->NodeIconSize = size; }
    inline void setEdgeSize(float size) { g_SpaceResources->EdgeSize = size; }
	inline void setTemperature(float temperature) { LOG("Temperature : %f\n", temperature); m_Temperature = temperature; }

	void checkNodeUID(Node::ID uid)
	{
		if (!m_NodeMap.containsRemoteID(uid))
		{
			LOG("Node UID %lu not found !\n", uid);
			throw;
		}
	}
	void checkLinkUID(Link::ID uid)
	{
		if (!m_LinkMap.containsRemoteID(uid))
		{
			LOG("Link UID %lu not found !\n", uid);
			throw;
		}
	}

    // ----- Graph Events ----

    void onSetAttribute(const std::string& name, VariableType type, const std::string& value)
    {
        FloatVariable vfloat;
        BooleanVariable vbool;
        Vec3Variable vvec3;
        StringVariable vstring;

        if (name == "space:update" && type == BOOLEAN)
        {
            applyDegreeTint();
        }
        else if (name == "space:animation" && type == BOOLEAN)
        {
            vbool.set(value);
            m_CameraAnimation = vbool.value();
        }
        else if (name == "space:linkmode" && type == STRING)
        {
            vstring.set(value);
            if (value == "node_color")
                g_SpaceResources->m_LinkMode = SpaceResources::NODE_COLOR;
            else if (value == "link_color")
                g_SpaceResources->m_LinkMode = SpaceResources::LINK_COLOR;
        }
        else if (name == "space:octree:update" && type == BOOLEAN)
        {
            vbool.set(value);

            LOG("[SPACE] Calculating bounding box ...\n");
            glm::vec3 min;
            glm::vec3 max;

            unsigned long count = 0;

            for (unsigned long i = 0; i < m_SpaceNodes.size(); i++)
            {
                if (m_SpaceNodes[i] == NULL)
                    continue;

                if (count == 0)
                {
                    min = max = m_SpaceNodes[i]->getPosition();
                    count++;
                    continue;
                }

                glm::vec3 pos = m_SpaceNodes[i]->getPosition();

                for (int i = 0; i < 3; i++)
                    min[i] = pos[i] < min[i] ? pos[i] : min[i];
                for (int i = 0; i < 3; i++)
                    max[i] = pos[i] > max[i] ? pos[i] : max[i];

                count++;
            }

            if (count == 0)
            {
                LOG("[SPACE] Graph is empty! Aborting.\n");
                return;
            }

            LOG("Min : (%f, %f, %f), Max : (%f, %f, %f)\n", min.x, min.y, min.z, max.x, max.y, max.z);

            LOG("[SPACE] Updating octree ...\n");
            SAFE_DELETE(m_Octree);
            m_Octree = new Octree(0.5f * (min + max), max - min);

            LOG("[SPACE] Inserting graph nodes in octree ...\n");
            for (unsigned long i = 0; i < m_SpaceNodes.size(); i++)
            {
                if (m_SpaceNodes[i] != NULL)
                    m_Octree->insert(new Scene::OctreeNode(m_SpaceNodes[i]));
            }
        }
        else
        {
            LOG("[SPACEVIEW] '%s' node attribute ignored !\n", name.c_str());
        }
    }

	void onAddNode(Node::ID uid, const char* label)
	{
		pushNodeVertexAround(uid, label, glm::vec3(0, 0, 0), 2);
	}

	void onRemoveNode(Node::ID uid)
	{
		checkNodeUID(uid);

		SpaceNode::ID vid = m_NodeMap.getLocalID(uid);

		for (unsigned int i = 0; i < m_SpaceLinks.size(); i++)
		{
			if (m_SpaceLinks[i] != NULL)
			{
				SpaceLink* link = static_cast<SpaceLink*>(m_SpaceLinks[i]->getDrawable());

				if (link->getNode1() == vid || link->getNode2() == vid)
				{
					m_SpaceLinks.remove(i);
					m_LinkMap.removeLocalID(i);

					LOG("Link %i removed with node\n", i);
				}
			}
		}

		// TODO : Remove node from spheres here

		m_SpaceNodes.remove(vid);
		m_NodeMap.eraseRemoteID(uid, vid);
	}

	void onSetNodeAttribute(Node::ID uid, const std::string& name, VariableType type, const std::string& value)
	{
		FloatVariable vfloat;
		BooleanVariable vbool;
		StringVariable vstring;
		Vec3Variable vvec3;
		Vec4Variable vvec4;

		checkNodeUID(uid);
		SpaceNode::ID id = m_NodeMap.getLocalID(uid);

		if (name == "space:locked" && type == BOOLEAN)
		{
			vbool.set(value);
			m_SpaceNodes[id]->setPositionLock(vbool.value());
		}
		else if ((name == "space:position" || name == "particles:position") && type == VEC3)
		{
			vvec3.set(value);
			m_SpaceNodes[id]->setPosition(vvec3.value());
		}
        else if (name == "space:color" && type == VEC3)
        {
            vvec3.set(value);
            static_cast<SpaceNode*>(m_SpaceNodes[id]->getDrawable())->setColor(vvec3.value());
        }
        else if (name == "space:color" && type == VEC4)
        {
            vvec4.set(value);
            static_cast<SpaceNode*>(m_SpaceNodes[id]->getDrawable())->setColor(vvec4.value());
        }
        else if (name == "space:lod" && type == FLOAT)
        {
            vfloat.set(value);
            static_cast<SpaceNode*>(m_SpaceNodes[id]->getDrawable())->setLOD(vfloat.value());
        }
        else if (name == "space:activity" && type == FLOAT)
        {
            vfloat.set(value);
            static_cast<SpaceNode*>(m_SpaceNodes[id]->getDrawable())->setActivity(vfloat.value());
        }
        else if (name == "space:icon" && type == STRING)
        {
            vstring.set(value);
            static_cast<SpaceNode*>(m_SpaceNodes[id]->getDrawable())->setIcon(vstring.value());
        }
		else
		{
			LOG("[SPACEVIEW] '%s' node attribute ignored !\n", name.c_str());
		}
	}

	void onSetNodeLabel(Node::ID uid, const char* label)
	{
		checkNodeUID(uid);

		SpaceNode::ID id = m_NodeMap.getLocalID(uid);

		static_cast<SpaceNode*>(m_SpaceNodes[id]->getDrawable())->setLabel(label);
	}

	void onSetNodeMark(Node::ID uid, unsigned int mark)
	{
		checkNodeUID(uid);

		SpaceNode::ID id = m_NodeMap.getLocalID(uid);

		static_cast<SpaceNode*>(m_SpaceNodes[id]->getDrawable())->setMark(mark);
	}

	void onSetNodeWeight(Node::ID uid, float weight)
	{
		checkNodeUID(uid);

		SpaceNode::ID id = m_NodeMap.getLocalID(uid);

		static_cast<SpaceNode*>(m_SpaceNodes[id]->getDrawable())->setSize(weight);
	}

	void onTagNode(Node::ID node, Sphere::ID sphere)
	{
		(void) node;
		(void) sphere;
	}

	void onAddLink(Link::ID uid, Node::ID uid1, Node::ID uid2)
	{
		checkNodeUID(uid1);
		checkNodeUID(uid2);

		SpaceNode::ID node1 = m_NodeMap.getLocalID(uid1);
		SpaceNode::ID node2 = m_NodeMap.getLocalID(uid2);

		SpaceLink::ID lid = m_SpaceLinks.add(new Scene::Node(new SpaceLink(m_SpaceNodes[node1], m_SpaceNodes[node2]), true));

		m_LinkMap.addRemoteID(uid, lid);
	}

	void onRemoveLink(Link::ID uid)
	{
		checkLinkUID(uid);

		SpaceLink::ID vid = m_LinkMap.getLocalID(uid);

		m_SpaceLinks.remove(vid);
		m_LinkMap.eraseRemoteID(uid, vid);
	}

	void onSetLinkAttribute(Link::ID uid, const std::string& name, VariableType type, const std::string& value)
	{
		checkLinkUID(uid);

		FloatVariable vfloat;
        Vec3Variable vvec3;
        Vec4Variable vvec4;

		SpaceLink::ID id = m_LinkMap.getLocalID(uid);

        if (name == "space:color" && type == VEC3)
        {
            checkLinkUID(uid);
            SpaceLink::ID id = m_LinkMap.getLocalID(uid);
            vvec3.set(value);
            static_cast<SpaceLink*>(m_SpaceLinks[id]->getDrawable())->setColor(0, vvec3.value());
            static_cast<SpaceLink*>(m_SpaceLinks[id]->getDrawable())->setColor(1, vvec3.value());
        }
        else if (name == "space:color" && type == VEC4)
        {
            checkLinkUID(uid);
            SpaceLink::ID id = m_LinkMap.getLocalID(uid);
            vvec4.set(value);
            static_cast<SpaceLink*>(m_SpaceLinks[id]->getDrawable())->setColor(0, vvec4.value());
            static_cast<SpaceLink*>(m_SpaceLinks[id]->getDrawable())->setColor(1, vvec4.value());
        }
		else if (name == "space:color1" && type == VEC4)
		{
			checkLinkUID(uid);
			SpaceLink::ID id = m_LinkMap.getLocalID(uid);
			vvec4.set(value);
			static_cast<SpaceLink*>(m_SpaceLinks[id]->getDrawable())->setColor(0, vvec4.value());
		}
		else if (name == "space:color2" && type == VEC4)
		{
			checkLinkUID(uid);
			SpaceLink::ID id = m_LinkMap.getLocalID(uid);
			vvec4.set(value);
			static_cast<SpaceLink*>(m_SpaceLinks[id]->getDrawable())->setColor(1, vvec4.value());
		}
        else if (name == "space:activity" && type == FLOAT)
        {
            vfloat.set(value);
            static_cast<SpaceLink*>(m_SpaceLinks[id]->getDrawable())->setActivity(vfloat.value());
        }
        else if (name == "space:lod" && type == FLOAT)
        {
            vfloat.set(value);
            static_cast<SpaceLink*>(m_SpaceLinks[id]->getDrawable())->setLOD(vfloat.value());
        }
		else
		{
			LOG("[SPACEVIEW] '%s' link attribute ignored !\n", name.c_str());
		}
	}

	void onAddSphere(Sphere::ID id, const char* label)
	{
		(void) id;
		(void) label;
		m_SpaceSpheres.add(new Scene::Node(new SpaceSphere(), true));
	}

	void onSetSphereMark(Sphere::ID id, unsigned int mark)
	{
		static_cast<SpaceSphere*>(m_SpaceSpheres[id]->getDrawable())->setColor(MarkerWidget::color(mark));
	}

	void onAddNeighbor(const std::pair<Node::ID, Link::ID>& element, const char* label, Node::ID neighbor)
	{
		checkNodeUID(neighbor);

		SpaceNode::ID nid = m_NodeMap.getLocalID(neighbor);

		SpaceNode::ID vid = pushNodeVertexAround(element.first, label, m_SpaceNodes[nid]->getPosition(), 2);

		Scene::Node* node = new Scene::Node(NULL, false);
		SpaceLink::ID lid = m_SpaceLinks.add(node);
		node->setDrawable(new SpaceLink(m_SpaceNodes[nid], m_SpaceNodes[vid]), true);

		m_LinkMap.addRemoteID(element.second, lid);
	}

	// -----

	inline Scene::NodeVector& getNodes() { return m_SpaceNodes; }
	inline Scene::NodeVector& getLinks() { return m_SpaceLinks; }
	inline Scene::NodeVector& getSpheres() { return m_SpaceSpheres; }

	inline NodeTranslationMap& getNodeMap() { return m_NodeMap; }
	inline LinkTranslationMap& getLinkMap() { return m_LinkMap; }

	inline GraphContext* context() { return static_cast<GraphContext*>(m_GraphEntity->context()); }
	inline GraphModel* model() { return static_cast<GraphModel*>(m_GraphEntity->model()); }

private:
	unsigned int m_WindowWidth;
	unsigned int m_WindowHeight;

	GraphEntity* m_GraphEntity;

	Clock m_Clock;
	Timecode m_LastUpdateTime;

	Camera m_Camera;
	bool m_CameraAnimation;

	NodeTranslationMap m_NodeMap;
	LinkTranslationMap m_LinkMap;

	Scene::NodeVector m_SpaceNodes;
	Scene::NodeVector m_SpaceLinks;
	Scene::NodeVector m_SpaceSpheres;

	Octree* m_Octree;

	RayPacket* m_RayPacket;

	PhysicsMode m_PhysicsMode;
	unsigned int m_Iterations;

	LinkAttractionForce m_LinkAttractionForce;
	NodeRepulsionForce m_NodeRepulsionForce;
	Physics::GravitationForce m_GravitationForce;
	DustAttractor m_DustAttractor;

	float m_Temperature;
};
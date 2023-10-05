#include "WalkMesh.hpp"

#include "read_write_chunk.hpp"

#include <glm/gtx/norm.hpp>
#include <glm/gtx/string_cast.hpp>

#include <iostream>
#include <fstream>
#include <algorithm>
#include <string>

WalkMesh::WalkMesh(std::vector< glm::vec3 > const &vertices_, std::vector< glm::vec3 > const &normals_, std::vector< glm::uvec3 > const &triangles_)
	: vertices(vertices_), normals(normals_), triangles(triangles_) {

	//construct next_vertex map (maps each edge to the next vertex in the triangle):
	next_vertex.reserve(triangles.size()*3);
	auto do_next = [this](uint32_t a, uint32_t b, uint32_t c) {
		auto ret = next_vertex.insert(std::make_pair(glm::uvec2(a,b), c));
		assert(ret.second);
	};
	for (auto const &tri : triangles) {
		do_next(tri.x, tri.y, tri.z);
		do_next(tri.y, tri.z, tri.x);
		do_next(tri.z, tri.x, tri.y);
	}

	//DEBUG: are vertex normals consistent with geometric normals?
	for (auto const &tri : triangles) {
		glm::vec3 const &a = vertices[tri.x];
		glm::vec3 const &b = vertices[tri.y];
		glm::vec3 const &c = vertices[tri.z];
		glm::vec3 out = glm::normalize(glm::cross(b-a, c-a));

		float da = glm::dot(out, normals[tri.x]);
		float db = glm::dot(out, normals[tri.y]);
		float dc = glm::dot(out, normals[tri.z]);
		da=da+db+dc;
	//	assert(da > 0.1f && db > 0.1f && dc > 0.1f);
	}
}

//project pt to the plane of triangle a,b,c and return the barycentric weights of the projected point:
glm::vec3 barycentric_weights(glm::vec3 const &a, glm::vec3 const &b, glm::vec3 const &c, glm::vec3 const &pt) {//reference from the screenshot shared in discord by Jim
	//TODO: implement!
	glm::vec3 v0=b-a;
	glm::vec3 v1=c-a;
	glm::vec3 v2=pt-a;
	
	float d00=glm::dot(v0,v0);
	float d01=glm::dot(v0,v1);
	float d11=glm::dot(v1,v1);
	float d20=glm::dot(v2,v0);
	float d21=glm::dot(v2,v1);
	
	float denominator=d00*d11-d01*d01;
	float v=(d11*d20-d01*d21)/denominator;
	float w=(d00*d21-d01* d20)/denominator;
	return glm::vec3(1.0f-v-w,v,w);
}

WalkPoint WalkMesh::nearest_walk_point(glm::vec3 const &world_point) const {
	assert(!triangles.empty() && "Cannot start on an empty walkmesh");

	WalkPoint closest;
	float closest_dis2 = std::numeric_limits< float >::infinity();

	for (auto const &tri : triangles) {
		//find closest point on triangle:

		glm::vec3 const &a = vertices[tri.x];
		glm::vec3 const &b = vertices[tri.y];
		glm::vec3 const &c = vertices[tri.z];

		//get barycentric coordinates of closest point in the plane of (a,b,c):
		glm::vec3 coords = barycentric_weights(a,b,c, world_point);

		//is that point inside the triangle?
		if (coords.x >= 0.0f && coords.y >= 0.0f && coords.z >= 0.0f) {
			//yes, point is inside triangle.
			float dis2 = glm::length2(world_point - to_world_point(WalkPoint(tri, coords)));
			if (dis2 < closest_dis2) {
				closest_dis2 = dis2;
				closest.indices = tri;
				closest.weights = coords;
			}
		} else {
			//check triangle vertices and edges:
			auto check_edge = [&world_point, &closest, &closest_dis2, this](uint32_t ai, uint32_t bi, uint32_t ci) {
				glm::vec3 const &a = vertices[ai];
				glm::vec3 const &b = vertices[bi];

				//find closest point on line segment ab:
				float along = glm::dot(world_point-a, b-a);
				float max = glm::dot(b-a, b-a);
				glm::vec3 pt;
				glm::vec3 coords;
				if (along < 0.0f) {
					pt = a;
					coords = glm::vec3(1.0f, 0.0f, 0.0f);
				} else if (along > max) {
					pt = b;
					coords = glm::vec3(0.0f, 1.0f, 0.0f);
				} else {
					float amt = along / max;
					pt = glm::mix(a, b, amt);
					coords = glm::vec3(1.0f - amt, amt, 0.0f);
				}

				float dis2 = glm::length2(world_point - pt);
				if (dis2 < closest_dis2) {
					closest_dis2 = dis2;
					closest.indices = glm::uvec3(ai, bi, ci);
					closest.weights = coords;
				}
			};
			check_edge(tri.x, tri.y, tri.z);
			check_edge(tri.y, tri.z, tri.x);
			check_edge(tri.z, tri.x, tri.y);
		}
	}
	assert(closest.indices.x < vertices.size());
	assert(closest.indices.y < vertices.size());
	assert(closest.indices.z < vertices.size());
	return closest;
}

//start at 'start.weights' on triangle 'start.indices'
// move by 'step' and...
//  ...if a wall is hit, report where + when
//  ...if no wall is hit, report final point + 1.0f
void WalkMesh::walk_in_triangle(WalkPoint const &start, glm::vec3 const &step, WalkPoint *end_, float *time_) const { //reference from the screenshot shared in discord by Jim
	assert(end_);
	auto &end = *end_;
	assert(time_);
	auto &time = *time_;
//!todo{
	glm::vec3 const &a = vertices[start.indices.x];
	glm::vec3 const &b = vertices[start.indices.y];
	glm::vec3 const &c = vertices[start.indices.z];
	
/*	auto to_world_point=[this](WalkPoint const&wp){
		return wp.weights[0]*vertices[wp.indices[0]]
			+wp.weights[1]*vertices[wp.indices[1]]
			+wp.weights[2]*vertices[wp.indices[2]];
	};
	auto barycentric_weights=[](glm::vec3 const &a,glm::vec3 const &b,glm::vec3 const &c,glm::vec3 const &pt){
		const glm::vec3 u=b-a;
		const glm::vec3 v=c-a;
		const glm::vec3 w=pt-a;
		const glm::vec3 n=glm::cross(u,v);
		const float n2=glm::dot(n,n);
		float c1=glm::dot(glm::cross(w,v),n)/n2;
		float c2=glm::dot(glm::cross(u,w),n)/n2;
		float c0=1-(c1+c2);
		return glm::vec3(c0,c1,c2);
	};
	*/
	
	glm::vec3 dest=to_world_point(start)+step;
	glm::vec3 destb=barycentric_weights(a,b,c,dest);
	float min_time=std::numeric_limits<float>::infinity();
	int min_coord=-1;
	auto foo=[&min_time,&min_coord](const unsigned int coord,const float d, const float s){
		if(d>0)return;
		float time=-s/(d-s);
		assert(time>=0.0f);
		if(time<min_time){
			min_time=time;
			min_coord=coord;
		}
	};
	foo(0,destb.x,start.weights.x);
	foo(1,destb.y,start.weights.y);
	foo(2,destb.z,start.weights.z);
	time=std::min(1.0f,min_time);
	//assert(time>0.0f);
	glm::vec3 weights=start.weights+time*(destb- start.weights);
	
	switch(min_coord){
		case 0:
			end.indices[0]=start.indices[1];
			end.indices[1]=start.indices[2];
			end.indices[2]=start.indices[0];
			end.weights[0]=weights[1];
			end.weights[1]=weights[2];
			end.weights[2]=0.0f;
			break;
		case 1:
			end.indices[0]=start.indices[2];
			end.indices[1]=start.indices[0];
			end.indices[2]=start.indices[1];
			end.weights[0]=weights[2];
			end.weights[1]=weights[0];
			end.weights[2]=0.0f;
			break;
		case 2:
			end.indices=start.indices;
			end.weights=weights;
			end.weights[2]=0.0f;
			break;
		default:
			end.indices=start.indices;
			end.weights=weights;
			break;
	}
	
	//TODO: transform 'step' into a barycentric velocity on (a,b,c)

	//TODO: check when/if this velocity pushes start.weights into an edge

//!}


}

//precondition: 'start' is on edge 'start.x -> start.y'
//  i.e. start.weights.z = 0
//
bool WalkMesh::cross_edge( WalkPoint const &start, WalkPoint *end_, glm::quat *rotation_ ) const {
	assert(start.weights.z == 0.0f);
	assert(start.indices.x <= vertices.size() && start.indices.y <= vertices.size() && start.indices.z <= vertices.size());
	assert(end_);
	auto &end = *end_;
	assert(rotation_);
	auto &rotation = *rotation_;
//!todo{
	//get start triangle
	//start.indices.x start.indices.y start.indices.z
	/*auto to_world_point=[this](WalkPoint const&wp){
		return wp.weights[0]*vertices[wp.indices[0]]
			+wp.weights[1]*vertices[wp.indices[1]]
			+wp.weights[2]*vertices[wp.indices[2]];
	};*/
	/*auto barycentric_weights=[](glm::vec3 const &a,glm::vec3 const &b,glm::vec3 const &c,glm::vec3 const &pt){
		const glm::vec3 u=b-a;
		const glm::vec3 v=c-a;
		const glm::vec3 w=pt-a;
		const glm::vec3 n=glm::cross(u,v);
		const float n2=glm::dot(n,n);
		float c1=glm::dot(glm::cross(w,v),n)/n2;
		float c2=glm::dot(glm::cross(u,w),n)/n2;
		float c0=1-(c1+c2);
		return glm::vec3(c0,c1,c2);
	};*/
	auto adjacent_triangle=[this](glm::uvec3 const &tri) {
		auto next_vertex=this->next_vertex;
		auto f = next_vertex.find(glm::uvec2(tri.y, tri.x));
		if (f != next_vertex.end()) {
			return glm::uvec3(tri.y, tri.x, f->second);
		} else {
			return glm::uvec3(-1U);
		}
	};
	glm::uvec3 nextMesh= adjacent_triangle(start.indices);
	if(nextMesh==glm::uvec3(-1U)){
		return false;
	}else{
		end.indices[0]=nextMesh.x;
		end.indices[1]=nextMesh.y;
		end.indices[2]=nextMesh.z;
		end.weights[0]=start.weights[1];
		end.weights[1]=start.weights[0];
		end.weights[2]=0.0f;
		auto v1=vertices[start.indices[0]];
		auto v2=vertices[start.indices[1]];
		auto v3=vertices[start.indices[2]];
		glm::vec3 normalA=glm::cross(v2-v1,v3-v1);
		normalA=glm::normalize(normalA);
		auto v4=vertices[end.indices[0]];
		auto v5=vertices[end.indices[1]];
		auto v6=vertices[end.indices[2]];
		glm::vec3 normalB=glm::cross(v5-v4,v6-v4);
		normalB=glm::normalize(normalB);
		rotation=glm::rotation(normalA,normalB);
		return true;
	}
	//TODO: check if edge (start.indices.x, start.indices.y) has a triangle on the other side:
	//  hint: remember 'next_vertex'!
	//TODO: if there is another triangle:
	//  TODO: set end's weights and indicies on that triangle:
	end = start;

	//  TODO: compute rotation that takes starting triangle's normal to ending triangle's normal:
	//  hint: look up 'glm::rotation' in the glm/gtx/quaternion.hpp header
	rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); //identity quat (wxyz init order)

	//return 'true' if there was another triangle, 'false' otherwise:
	return false;

//!}


}


WalkMeshes::WalkMeshes(std::string const &filename) {
	std::ifstream file(filename, std::ios::binary);

	std::vector< glm::vec3 > vertices;
	read_chunk(file, "p...", &vertices);

	std::vector< glm::vec3 > normals;
	read_chunk(file, "n...", &normals);

	std::vector< glm::uvec3 > triangles;
	read_chunk(file, "tri0", &triangles);

	std::vector< char > names;
	read_chunk(file, "str0", &names);

	struct IndexEntry {
		uint32_t name_begin, name_end;
		uint32_t vertex_begin, vertex_end;
		uint32_t triangle_begin, triangle_end;
	};

	std::vector< IndexEntry > index;
	read_chunk(file, "idxA", &index);

	if (file.peek() != EOF) {
		std::cerr << "WARNING: trailing data in walkmesh file '" << filename << "'" << std::endl;
	}

	//-----------------

	if (vertices.size() != normals.size()) {
		throw std::runtime_error("Mis-matched position and normal sizes in '" + filename + "'");
	}

	for (auto const &e : index) {
		if (!(e.name_begin <= e.name_end && e.name_end <= names.size())) {
			throw std::runtime_error("Invalid name indices in index of '" + filename + "'");
		}
		if (!(e.vertex_begin <= e.vertex_end && e.vertex_end <= vertices.size())) {
			throw std::runtime_error("Invalid vertex indices in index of '" + filename + "'");
		}
		if (!(e.triangle_begin <= e.triangle_end && e.triangle_end <= triangles.size())) {
			throw std::runtime_error("Invalid triangle indices in index of '" + filename + "'");
		}

		//copy vertices/normals:
		std::vector< glm::vec3 > wm_vertices(vertices.begin() + e.vertex_begin, vertices.begin() + e.vertex_end);
		std::vector< glm::vec3 > wm_normals(normals.begin() + e.vertex_begin, normals.begin() + e.vertex_end);

		//remap triangles:
		std::vector< glm::uvec3 > wm_triangles; wm_triangles.reserve(e.triangle_end - e.triangle_begin);
		for (uint32_t ti = e.triangle_begin; ti != e.triangle_end; ++ti) {
			if (!( (e.vertex_begin <= triangles[ti].x && triangles[ti].x < e.vertex_end)
			    && (e.vertex_begin <= triangles[ti].y && triangles[ti].y < e.vertex_end)
			    && (e.vertex_begin <= triangles[ti].z && triangles[ti].z < e.vertex_end) )) {
				throw std::runtime_error("Invalid triangle in '" + filename + "'");
			}
			wm_triangles.emplace_back(
				triangles[ti].x - e.vertex_begin,
				triangles[ti].y - e.vertex_begin,
				triangles[ti].z - e.vertex_begin
			);
		}
		
		std::string name(names.begin() + e.name_begin, names.begin() + e.name_end);

		auto ret = meshes.emplace(name, WalkMesh(wm_vertices, wm_normals, wm_triangles));
		if (!ret.second) {
			throw std::runtime_error("WalkMesh with duplicated name '" + name + "' in '" + filename + "'");
		}

	}
}

WalkMesh const &WalkMeshes::lookup(std::string const &name) const {
	auto f = meshes.find(name);
	if (f == meshes.end()) {
		throw std::runtime_error("WalkMesh with name '" + name + "' not found.");
	}
	return f->second;
}

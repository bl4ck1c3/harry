#pragma once

#include <unordered_set>
#include <iostream>
#include <cmath>

#include "cutborder.h"
// #include "attrcode.h"
#include "io.h"

#include "../../structs/mesh.h"
#include "../../assert.h"

#include "attrcode.h"


#define INVALID_PAIR mesh::conn::fepair()

struct Perm {
	std::vector<int> perm;

	inline Perm(int n) : perm(n, -1)
	{}

	inline bool isMapped(int idx)
	{
		return perm[idx] != -1;
	}

	inline void map(int a, int b)
	{
		perm[a] = b;
	}
	inline void unmap(int a)
	{
		perm[a] = -1;
	}
	inline int get(int idx)
	{
		return perm[idx];
	}
};

struct CoderData {
	mesh::conn::fepair a;
	int vr;

	inline CoderData() : a(INVALID_PAIR)
	{}

	inline void init(mesh::conn::fepair _a, int _vr)
	{
		a = _a;
		vr = _vr;
	}
};
inline std::ostream &operator<<(std::ostream &os, const CoderData &v)
{
	return os << v.a;
}
/*
sequence element edge validness:

seq_.. | frist |  mid  | last
-------+-------+-------+------
e0     |  YES  |       |
e1     |  YES  |  YES  |  YES
e2     |       |       |  YES
*/

struct CBMStats {
	int used_parts, used_elements;
	int nm;
};

struct Triangulator {
	std::unordered_set<mesh::faceidx_t> remaining_faces;
	const mesh::conn::Conn &conn;

	inline Triangulator(const mesh::Mesh &mesh) : conn(mesh.conn)
	{
		for (mesh::faceidx_t i = 0; i < mesh.num_face(); ++i) {
			remaining_faces.insert(i);
		}
	}

	inline mesh::conn::fepair chooseTriangle()
	{
		mesh::faceidx_t f = remaining_faces.find(0) == remaining_faces.end() ? *remaining_faces.begin() : 0;
		mesh::conn::fepair a = mesh::conn::fepair(f, 0);
		remaining_faces.erase(f);
		return a;
	}

	inline mesh::conn::fepair getVertexData(mesh::conn::fepair i)
	{
		mesh::conn::fepair a = conn.twin(i);
		if (a == i) return INVALID_PAIR;
		if (remaining_faces.find(a.f()) == remaining_faces.end()) return INVALID_PAIR; // this can happen on non manifold edges
		remaining_faces.erase(a.f());
		return a;
	}

	inline bool empty()
	{
		return remaining_faces.empty();
	}
};


template <typename H, typename T, typename W, typename P>
CBMStats encode(H &mesh, T &handle, W &wr, attrcode::AttrCoder<W> &ac, P &prog)
{

	typedef DataTpl<CoderData> Data;
	typedef ElementTpl<CoderData> Element;
	CutBorder<CoderData> cutBorder(100 + 20000, 10 * std::sqrt(mesh.num_vtx()) + 10000000); // TODO: wrong assumption in CBM paper
	Triangulator tri(mesh);

	mesh::vtxidx_t vertexIdx = 0;
	mesh::faceidx_t fop;
	Perm perm(mesh.num_vtx());
	std::cout << mesh.num_vtx() << " " << mesh.num_face() << " " << mesh.conn.num_face() << std::endl;
	std::vector<int> order(mesh.num_vtx(), 0);
	mesh::faceidx_t f;

	prog.start(mesh.num_vtx());
	int nm = 0;
	int curtri, ntri;
	mesh::conn::fepair curedge, startedge, lastfaceedge;
	mesh::conn::fepair e0, e1, e2;
	do {
		Data v0, v1, v2, v2op;
		curtri = 0;
		e0 = tri.chooseTriangle(); e1 = mesh.conn.enext(e0); e2 = mesh.conn.enext(e1);
		v0 = Data(mesh.conn.org(e0)); v1 = Data(mesh.conn.org(e1)); v2 = Data(mesh.conn.org(e2));
		bool m0 = perm.isMapped(v0.idx), m1 = perm.isMapped(v1.idx), m2 = perm.isMapped(v2.idx);
		f = mesh.conn.face(e0);
		ntri = mesh.conn.num_edges(f) - 2;
		assert_eq(ntri, 1);
		CutBorderBase::INITOP initop;
		nm += m0 + m1 + m2;
		// Tri 3
		if (m0 && m1 && m2) {
			wr.tri111(ntri, perm.get(v0.idx), perm.get(v1.idx), perm.get(v2.idx));
// 			ac.face(f);
// 			ac.face(f, e0.e());
// 			ac.vtx(f, e0.e());
			initop = CutBorderBase::TRI111;
		}
		// Tri 2
		else if (m0 && m1) {
			wr.tri110(ntri, perm.get(v0.idx), perm.get(v1.idx));
// 			ac.face(f);
// 			ac.face(f, e0.e());
			ac.vtx(f, e2.e());
			assert_eq(mesh.conn.org(e2), v2.idx);
			perm.map(v2.idx, vertexIdx++);
			initop = CutBorderBase::TRI110;
		} else if (m1 && m2) {
			wr.tri011(ntri, perm.get(v1.idx), perm.get(v2.idx));
// 			ac.face(f);
// 			ac.face(f, e0.e());
			ac.vtx(f, e0.e());
			assert_eq(mesh.conn.org(e0), v0.idx);
			perm.map(v0.idx, vertexIdx++);
			initop = CutBorderBase::TRI011;
		} else if (m2 && m0) {
			wr.tri101(ntri, perm.get(v2.idx), perm.get(v0.idx));
// 			ac.face(f);
// 			ac.face(f, e0.e());
			ac.vtx(f, e1.e());
			assert_eq(mesh.conn.org(e1), v1.idx);
			perm.map(v1.idx, vertexIdx++);
			initop = CutBorderBase::TRI101;
		}
		// Tri 1
		else if (m0) {
			wr.tri100(ntri, perm.get(v0.idx));
// 			ac.face(f);
// 			ac.face(f, e0.e());
			ac.vtx(f, e1.e());
			ac.vtx(f, e2.e());
			assert_eq(mesh.conn.org(e1), v1.idx);
			assert_eq(mesh.conn.org(e2), v2.idx);
			perm.map(v1.idx, vertexIdx++); perm.map(v2.idx, vertexIdx++);
			initop = CutBorderBase::TRI100;
		} else if (m1) {
			wr.tri010(ntri, perm.get(v1.idx));
// 			ac.face(f);
// 			ac.face(f, e0.e());
			ac.vtx(f, e2.e());
			ac.vtx(f, e0.e());
			assert_eq(mesh.conn.org(e2), v2.idx);
			assert_eq(mesh.conn.org(e0), v0.idx);
			perm.map(v2.idx, vertexIdx++); perm.map(v0.idx, vertexIdx++);
			initop = CutBorderBase::TRI010;
		} else if (m2) {
			wr.tri001(ntri, perm.get(v2.idx));
// 			ac.face(f);
// 			ac.face(f, e0.e());
			ac.vtx(f, e0.e());
			ac.vtx(f, e1.e());
			assert_eq(mesh.conn.org(e0), v0.idx);
			assert_eq(mesh.conn.org(e1), v1.idx);
			perm.map(v0.idx, vertexIdx++); perm.map(v1.idx, vertexIdx++);
			initop = CutBorderBase::TRI001;
		}
		// Tri 0 = Initial
		else {
			wr.initial(ntri);
// 			ac.face(f);
// 			ac.face(f, e0.e());
			ac.vtx(f, e0.e());
			ac.vtx(f, e1.e());
			ac.vtx(f, e2.e());
			perm.map(v0.idx, vertexIdx++); perm.map(v1.idx, vertexIdx++); perm.map(v2.idx, vertexIdx++);
			initop = CutBorderBase::INIT;
		}

		handle(f, curtri, ntri, v0.idx, v1.idx, v2.idx, initop);

		++order[v0.idx]; ++order[v1.idx]; ++order[v2.idx];

// 		ac.wedge(f, 0, v0.idx); ac.wedge(f, 1, v1.idx); ac.wedge(f, 2, v2.idx);

		v0.init(e0, v2.idx);
		v1.init(e1, v0.idx);
		v2.init(e2, v1.idx);
		cutBorder.initial(v0, v1, v2);
// 		cutBorder.init(cbe0->next->next, ntri == 1 ? e2 : INVALID_PAIR, v1.idx);


		startedge = e0; curedge = e1;
		lastfaceedge = e2;
		++curtri;

		while (!cutBorder.atEnd()) {
			Element *elm_gate = cutBorder.traverseStep(v0, v1);
			Data *data_gate = &elm_gate->data;
			mesh::conn::fepair gate = data_gate->a;
			mesh::conn::fepair gateedgeprev = elm_gate->prev->data.a;
			mesh::conn::fepair gateedgenext = elm_gate->next->data.a;
			bool seq_first = curtri == ntri;
			if (seq_first) {
				lastfaceedge = gate;

				assert_eq(v0.idx, mesh.conn.org(gate));
				assert_eq(v1.idx, mesh.conn.dest(gate));
// 				v2op = Data(mesh.dest(mesh.enext(gate))); // TODO error here!!!
				v2op = data_gate->vr;

				e0 = tri.getVertexData(gate);
				startedge = e0;
			} else {
				assert_eq(v0.idx, mesh.conn.dest(curedge));
				assert_eq(v1.idx, mesh.conn.org(startedge));
// 				v2op = Data(mesh.org(curedge));
				v2op = data_gate->vr;
				e0 = INVALID_PAIR;
			}

			wr.order(order[v1.idx]);

			if (seq_first && e0 == INVALID_PAIR) {
				handle(f, 0, ntri, v0.idx, v1.idx, v2op.idx, CutBorderBase::BORDER);
				f = mesh.conn.face(gate);
				CutBorderBase::OP bop = cutBorder.border();

				if (mesh.conn.twin(gate) != gate) mesh.conn.swap(gate, gate); // fix bad border

// 				bop = CutBorderBase::BORDER;
				wr.border(bop);
			} else {
				if (seq_first) {
					curtri = 0;
					f = mesh.conn.face(e0);
					fop = mesh.conn.face(gate);
					ntri = mesh.conn.num_edges(f) - 2;

					e1 = curedge = mesh.conn.enext(e0); e2 = mesh.conn.enext(e1);
					
					assert_eq(mesh.conn.org(e0), v1.idx);
					assert_eq(mesh.conn.dest(e0), v0.idx);
					v2 = Data(mesh.conn.org(e2));
				} else {
					f = mesh.conn.face(startedge); // TODO: not needed
					fop = mesh.conn.face(lastfaceedge);

					e1 = curedge = mesh.conn.enext(curedge);
					e2 = mesh.conn.enext(e1);

					v2 = Data(mesh.conn.dest(curedge));
				}
				bool seq_last = curtri + 1 == ntri;
				bool seq_mid = !seq_first && !seq_last;

				if (!perm.isMapped(v2.idx)) {
					handle(f, curtri, ntri, v1.idx, v0.idx, v2.idx, CutBorderBase::ADDVTX);
					cutBorder.newVertex(v2);
					cutBorder.last->init(e2, v0.idx);
					data_gate->init(e1, v1.idx);
					wr.newvertex(seq_first ? ntri : 0); // TODO
// 					if (seq_first) ac.face(f);
// 					if (seq_first) ac.face(f, e0.e());
					ac.vtx(f, e2.e());
					perm.map(v2.idx, vertexIdx++);
				} else {
					int i, p;
					CutBorderBase::OP op;
					bool succ = cutBorder.findAndUpdate(v2, i, p, op);
					if (!succ) {
						handle(f, curtri, ntri, v1.idx, v0.idx, v2.idx, CutBorderBase::NM);
						cutBorder.newVertex(v2);
						cutBorder.last->init(e2, v0.idx);
						data_gate->init(e1, v1.idx);
						wr.nm(seq_first ? ntri : 0, perm.get(v2.idx));
// 						if (seq_first) ac.face(f, e0.e());
// 						if (seq_first) ac.face(f);
						++nm;
// 						e0 = mesh::Mesh::INVALID_PAIR;
					} else if (op == CutBorderBase::UNION) {
						assert(seq_last);
						handle(f, curtri, ntri, v1.idx, v0.idx, v2.idx, CutBorderBase::UNION);
						wr.cutborderunion(seq_first ? ntri : 0, i, p);
// 						if (seq_first) ac.face(f, e0.e());
// 						if (seq_first) ac.face(f);
						cutBorder.last->init(e2, v0.idx);
						data_gate->init(e1, v1.idx);
					} else if (op == CutBorderBase::CONNFWD || op == CutBorderBase::CLOSEFWD) {
						assert(seq_last);
						handle(f, curtri, ntri, v1.idx, v0.idx, v2.idx, CutBorderBase::CONNFWD);
						if (mesh.conn.twin(gateedgenext) != e2) mesh.conn.swap(gateedgenext, e2);
						if (op == CutBorderBase::CLOSEFWD && mesh.conn.twin(gateedgeprev) != e1) mesh.conn.swap(gateedgeprev, e1);
						wr.connectforward(seq_first ? ntri : 0);
// 						if (seq_first) ac.face(f, e0.e());
// 						if (seq_first) ac.face(f);
						data_gate->init(e1, v1.idx);
					} else if (op == CutBorderBase::CONNBWD || op == CutBorderBase::CLOSEBWD) {
						assert(seq_last);
						handle(f, curtri, ntri, v1.idx, v0.idx, v2.idx, CutBorderBase::CONNBWD);
						if (mesh.conn.twin(gateedgeprev) != e1) mesh.conn.swap(gateedgeprev, e1);
						if (op == CutBorderBase::CLOSEBWD && mesh.conn.twin(gateedgenext) != e2) mesh.conn.swap(gateedgenext, e2);
						wr.connectbackward(seq_first ? ntri : 0);
// 						if (seq_first) ac.face(f, e0.e());
// 						if (seq_first) ac.face(f);
						data_gate->init(e2, v0.idx);
					} else {
						assert(seq_last);
						handle(f, curtri, ntri, v1.idx, v0.idx, v2.idx, CutBorderBase::SPLIT);
						wr.splitcutborder(seq_first ? ntri : 0, i);
// 						if (seq_first) ac.face(f, e0.e());
// 						if (seq_first) ac.face(f);
						cutBorder.last->init(e2, v0.idx);
						data_gate->init(e1, v1.idx);
					}
					assert_eq(curtri + 1 == ntri, seq_last); 
					if (seq_last) cutBorder.preserveOrder();
				}

				++order[v0.idx]; ++order[v1.idx]; ++order[v2.idx];


				if (seq_first) {
// 					ac.wedge(f, 0, v1.idx); ac.wedge(f, 1, v0.idx); ac.wedge(f, 2, v2.idx);
				} else {
// 					ac.wedge(f, curtri + 2, /* mesh.org(f, curtri + 2)*/v2.idx);
				}

				++curtri;
			}

			prog(vertexIdx);
		}
	} while (!tri.empty());
	wr.end();
	prog.end();
// 	std::cout << "processed faces: " << f << " empty: " << tri.empty() << std::endl;

	return CBMStats{ cutBorder.max_parts, cutBorder.max_elements, nm };
}

template <typename R, typename P>
CBMStats decode(mesh::Builder &builder, R &rd, attrcode::AttrDecoder<R> &ac, P &prog)
{
	builder.noautomerge();

	typedef DataTpl<CoderData> Data;
	typedef ElementTpl<CoderData> Element;

	int nm = 0;

	CutBorder<CoderData> cutBorder(100 + 20000, 10 * sqrt(builder.num_vtx()) + 10000000);
	std::vector<int> order(builder.num_vtx(), 0);

	int vertexIdx = 0;
	mesh::faceidx_t f = 0;
	int i, p;

	prog.start(builder.num_vtx());

	int curtri, ntri;
	mesh::conn::fepair startedge, curedge, lastfaceedge;
	do {
		Data v0, v1, v2;
		CutBorderBase::INITOP initop = rd.iop();
		if (initop == CutBorderBase::EOM) break;
		curtri = 0;
		switch (initop) {
		case CutBorderBase::INIT:
			v0.idx = vertexIdx++; v1.idx = vertexIdx++; v2.idx = vertexIdx++;
			break;
		case CutBorderBase::TRI100:
			v0.idx = rd.vertid(); v1.idx = vertexIdx++; v2.idx = vertexIdx++;
			break;
		case CutBorderBase::TRI010:
			v2.idx = vertexIdx++; v1.idx = rd.vertid(); v0.idx = vertexIdx++;
			break;
		case CutBorderBase::TRI001:
			v0.idx = vertexIdx++; v1.idx = vertexIdx++; v2.idx = rd.vertid();
			break;
		case CutBorderBase::TRI110:
			v0.idx = rd.vertid(); v1.idx = rd.vertid(); v2.idx = vertexIdx++;
			break;
		case CutBorderBase::TRI101:
			v2.idx = rd.vertid(); v1.idx = vertexIdx++; v0.idx = rd.vertid();
			break;
		case CutBorderBase::TRI011:
			v0.idx = vertexIdx++; v1.idx = rd.vertid(); v2.idx = rd.vertid();
			break;
		case CutBorderBase::TRI111:
			v0.idx = rd.vertid(); v1.idx = rd.vertid(); v2.idx = rd.vertid();
			break;
		}
		ntri = rd.numtri();
		++order[v0.idx]; ++order[v1.idx]; ++order[v2.idx];

		builder.face_begin(ntri + 2); builder.set_org(v0.idx); builder.set_org(v1.idx); builder.set_org(v2.idx);

		++curtri;
		if (curtri == ntri) builder.face_end();

		switch (initop) {
		case CutBorderBase::INIT:
			ac.vtx(f, 0); ac.vtx(f, 1); ac.vtx(f, 2);
			break;
		case CutBorderBase::TRI100:
			ac.vtx(f, 1); ac.vtx(f, 2);
			break;
		case CutBorderBase::TRI010:
			ac.vtx(f, 2); ac.vtx(f, 0);
			break;
		case CutBorderBase::TRI001:
			ac.vtx(f, 0); ac.vtx(f, 1);
			break;
		case CutBorderBase::TRI110:
			ac.vtx(f, 2);
			break;
		case CutBorderBase::TRI101:
			ac.vtx(f, 1);
			break;
		case CutBorderBase::TRI011:
			ac.vtx(f, 0);
			break;
		case CutBorderBase::TRI111:
			break;
		}

		mesh::conn::fepair e0(f, 0), e1(f, 1), e2(f, 2);
		v0.init(e0, v2.idx);
		v1.init(e1, v0.idx);
		v2.init(e2, v1.idx);

		assert_eq(builder.mesh.conn.org(e0), v0.idx);
		assert_eq(builder.mesh.conn.org(e1), v1.idx);
		assert_eq(builder.mesh.conn.org(e2), v2.idx);
		cutBorder.initial(v0, v1, v2);

		if (curtri == ntri) ++f;

		startedge = e0; curedge = e1;
		lastfaceedge = e2;

		while (!cutBorder.atEnd()) {
			Element *elm_gate = cutBorder.traverseStep(v0, v1);
			Data *data_gate = &elm_gate->data;
			mesh::vtxidx_t gatevr = data_gate->vr;
			mesh::conn::fepair gateedge = data_gate->a;
			mesh::conn::fepair gateedgeprev = elm_gate->prev->data.a;
			mesh::conn::fepair gateedgenext = elm_gate->next->data.a;
// 			mesh::faceidx_t gateface = /*gateedge.f()*/data_gate->f;

			rd.order(order[v1.idx]);

			CutBorderBase::OP op = rd.op(), realop;

			bool seq_first = curtri == ntri;

			switch (op) {
			case CutBorderBase::CONNFWD:
				assert(seq_last);
				v2 = cutBorder.connectForward(realop); // TODO: add border to realop
				break;
			case CutBorderBase::CONNBWD:
				assert(seq_last);
				v2 = cutBorder.connectBackward(realop); // TODO: add border to realop
				break;
			case CutBorderBase::SPLIT:
				assert(seq_last);
				i = rd.elem();
				v2 = cutBorder.splitCutBorder(i);
				break;
			case CutBorderBase::UNION:
				assert(seq_last);
				i = rd.elem();
				p = rd.part();
				v2 = cutBorder.cutBorderUnion(i, p);
				break;
			case CutBorderBase::ADDVTX:
				v2 = Data(vertexIdx++);
				assert_eq(i0, v2.idx);
				cutBorder.newVertex(v2);
				break;
			case CutBorderBase::NM:
				v2.idx = rd.vertid();
				cutBorder.newVertex(v2);
				++nm;
				break;
			case CutBorderBase::BORDER:
				cutBorder.border();
				v2 = Data(-1);
				assert_eq(builder.mesh.conn.twin(gateedge), gateedge);
				break;
			}

			if (!v2.isUndefined()) {
				if (seq_first) {
					ntri = rd.numtri();
					curtri = 0;
					builder.face_begin(ntri + 2);
					builder.set_org(v1.idx);
					builder.set_org(v0.idx);
					builder.set_org(v2.idx);
					e0 = mesh::conn::fepair(f, 0); e1 = curedge = mesh::conn::fepair(f, 1); e2 = mesh::conn::fepair(f, 2);
				} else {
					builder.set_org(v2.idx);
					e0 = INVALID_PAIR;
					e1 = curedge = builder.mesh.conn.enext(curedge);
					e2 = builder.mesh.conn.enext(e1);
				}
				bool seq_last = curtri + 1 == ntri;
				bool seq_mid = !seq_first && !seq_last;
				assert_eq(builder.builder_conn.cur_f, f);

				switch (op) {
				case CutBorderBase::CONNFWD:
					data_gate->init(e1, v1.idx);
					break;
				case CutBorderBase::CONNBWD:
					data_gate->init(e2, v0.idx);
					break;
				case CutBorderBase::SPLIT:
				case CutBorderBase::UNION:
				case CutBorderBase::ADDVTX:
				case CutBorderBase::NM:
					cutBorder.last->init(e2, v0.idx);
					data_gate->init(e1, v1.idx);
					break;
				}

				++order[v0.idx]; ++order[v1.idx]; ++order[v2.idx];

// 				if (seq_first) {
// 					ac.wedge(f, 0, v1.idx); ac.wedge(f, 1, v0.idx); ac.wedge(f, 2, v2.idx);
// 				} else {
// 					ac.wedge(f, curtri + 2, v2.idx);
// 				}
				if (op == CutBorderBase::ADDVTX) ac.vtx(f, curtri + 2);
				++curtri;
				if (seq_last) {
					builder.face_end();
					cutBorder.preserveOrder();
					++f;
				}

				if (seq_first) {
					assert_eq(builder.mesh.conn.twin(gateedge), gateedge);
					assert_eq(builder.mesh.conn.twin(e0), e0);
					builder.mesh.conn.fmerge(gateedge, e0);
				}

				switch (op) {
				case CutBorderBase::CONNFWD:
					assert_eq(builder.mesh.conn.twin(gateedgenext), gateedgenext);
					assert_eq(builder.mesh.conn.twin(e2), e2);
					builder.mesh.conn.fmerge(gateedgenext, e2);

					if (realop == CutBorderBase::CLOSEFWD) {
						assert_eq(builder.mesh.conn.twin(gateedgeprev), gateedgeprev);
						assert_eq(builder.mesh.conn.twin(e1), e1);
						builder.mesh.conn.fmerge(gateedgeprev, e1);
					}
					break;
				case CutBorderBase::CONNBWD:
					assert_eq(builder.mesh.conn.twin(gateedgeprev), gateedgeprev);
					assert_eq(builder.mesh.conn.twin(e1), e1);
					builder.mesh.conn.fmerge(gateedgeprev, e1);

					if (realop == CutBorderBase::CLOSEBWD) {
						assert_eq(builder.mesh.conn.twin(gateedgenext), gateedgenext);
						assert_eq(builder.mesh.conn.twin(e2), e2);
						builder.mesh.conn.fmerge(gateedgenext, e2);
					}
					break;
				}
			}

			prog(vertexIdx);
		}
	} while (1);
	prog.end();

	return CBMStats{ cutBorder.max_parts, cutBorder.max_elements, nm };
}


//358780bytes for bunny
//297664bytes after diff
//24063bytes
//298018bytes
//297880bytes
//271340
//271354bytes
//261379bytes
//261363bytes
//261364
//261375
//409383 with attr

// globe:
//1915068
//1915024 // NM OP introduced
//1913730 // removed 2 redudant vertids
//1913734
//1912111 // nm changed border to newvertex
//1912116
//1911926 // sperated to init-symbol context
//1911914
//1890733 // fixed symbol model
//4260149 with attributes
//4065502

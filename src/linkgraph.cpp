/*
 * graph.cpp
 *
 *  Created on: 28.02.2009
 *      Author: alve
 */


#include "linkgraph.h"
#include "settings_type.h"
#include "station_func.h"
#include "date_func.h"
#include "variables.h"
#include "map_func.h"
#include "demands.h"
#include "mcf.h"
#include "core/bitmath_func.hpp"
#include <limits>
#include <queue>

LinkGraph _link_graphs[NUM_CARGO];

typedef std::map<StationID, NodeID> ReverseNodeIndex;

bool LinkGraph::NextComponent()
{
	ReverseNodeIndex index;
	NodeID node = 0;
	std::queue<Station *> search_queue;
	Component * component = NULL;
	while (true) {
		// find first station of next component
		if (station_colours[current_station] > USHRT_MAX / 2 && IsValidStationID(current_station)) {
			Station * station = GetStation(current_station);
			LinkStatMap & links = station->goods[cargo].link_stats;
			if (!links.empty()) {
				if (++current_colour == USHRT_MAX / 2) {
					current_colour = 0;
				}
				search_queue.push(station);
				station_colours[current_station] = current_colour;
				component = new Component(current_colour);
				GoodsEntry & good = station->goods[cargo];
				node = component->AddNode(current_station, good.supply, HasBit(good.acceptance_pickup, GoodsEntry::ACCEPTANCE));
				index[current_station++] = node;
				break; // found a station
			}
		}
		if (++current_station == GetMaxStationIndex()) {
			current_station = 0;
			return false;
		}
	}
	// find all stations belonging to the current component
	while(!search_queue.empty()) {
		Station * target = search_queue.front();
		StationID target_id = target->index;
		search_queue.pop();
		GoodsEntry & good = target->goods[cargo];
		LinkStatMap & links = good.link_stats;
		for(LinkStatMap::iterator i = links.begin(); i != links.end(); ++i) {
			StationID source_id = i->first;
			Station * source = GetStation(i->first);
			LinkStat & link_stat = i->second;
			if (station_colours[source_id] != current_colour) {
				station_colours[source_id] = current_colour;
				search_queue.push(source);
				GoodsEntry & good = source->goods[cargo];
				node = component->AddNode(source_id, good.supply, HasBit(good.acceptance_pickup, GoodsEntry::ACCEPTANCE));
				index[source_id] = node;
			} else {
				node = index[source_id];
			}
			component->AddEdge(node, index[target_id], link_stat.capacity);
		}
	}
	// here the list of nodes and edges for this component is complete.
	component->CalculateDistances();
	jobs.push_back(component);
	jobs.back().SpawnThread(cargo);
	return true;
}

void LinkGraph::InitColours()
{
	for (uint i = 0; i < Station_POOL_MAX_BLOCKS; ++i) {
		station_colours[i] = USHRT_MAX;
	}
}


void OnTick_LinkGraph()
{
	if ((_tick_counter + LinkGraph::COMPONENTS_TICK) % DAY_TICKS == 0) {
		for(CargoID cargo = CT_BEGIN; cargo != CT_END; ++cargo) {
			if ((_date + cargo) % _settings_game.economy.linkgraph_recalc_interval == 0) {
				LinkGraph & graph = _link_graphs[cargo];
				if (!graph.NextComponent()) {
					graph.Join();
				}
			}
		}
	}
}

LinkGraph::LinkGraph()  : current_colour(0), current_station(0), cargo(CT_INVALID)
{
	for (CargoID i = CT_BEGIN; i != CT_END; ++i) {
		if (this == &(_link_graphs[i])) {
			cargo = i;
		}
	}
	InitColours();
}

uint Component::AddNode(StationID st, uint supply, uint demand) {
	nodes.push_back(Node(st, supply, demand));
	for(NodeID i = 0; i < num_nodes; ++i) {
		edges[i].push_back(Edge());
	}
	edges.push_back(std::vector<Edge>(++num_nodes));
	return num_nodes - 1;
}

void Component::AddEdge(NodeID from, NodeID to, uint capacity) {
	edges[from][to].capacity = capacity;
}

void Component::CalculateDistances() {
	for(NodeID i = 0; i < num_nodes; ++i) {
		for(NodeID j = 0; j < i; ++j) {
			Station * st1 = GetStation(nodes[i].station);
			Station * st2 = GetStation(nodes[j].station);
			uint distance = DistanceManhattan(st1->xy, st2->xy);
			edges[i][j].distance = distance;
			edges[j][i].distance = distance;
		}
	}
}

void Component::SetSize(uint size) {
	num_nodes = size;
	nodes.resize(num_nodes);
	edges.resize(num_nodes, std::vector<Edge>(num_nodes));
}

Component::Component(colour col) :
	num_nodes(0),
	component_colour(col)
{
}

Component::Component(uint size, colour c) :
	num_nodes(size),
	component_colour(c),
	nodes(size),
	edges(size, std::vector<Edge>(size))
{
}

bool LinkGraph::Join() {
	if (jobs.empty()) {
		return false;
	}
	LinkGraphJob & job = jobs.front();

	if (job.GetJoinTime() > _tick_counter) {
		return false;
	}

	Component * comp = job.GetComponent();

	for(NodeID i = 0; i < comp->GetSize(); ++i) {
		Node & node = comp->GetNode(i);
		StationID id = node.station;
		station_colours[id] += USHRT_MAX / 2;
		if (id < current_station) current_station = id;
	}
	jobs.pop_front();
	return true;
}

void LinkGraph::AddComponent(Component * component, uint join) {
	 colour component_colour = component->GetColour();
	 for(NodeID i = 0; i < component->GetSize(); ++i) {
		 station_colours[component->GetNode(i).station] = component_colour;
	 }
	 jobs.push_back(LinkGraphJob(component, join));
	 jobs.back().SpawnThread(cargo);
}

void LinkGraphJob::Run() {
	for (HandlerList::iterator i = handlers.begin(); i != handlers.end(); ++i) {
		ComponentHandler * handler = *i;
		handler->Run(component);
	}
}

LinkGraphJob::~LinkGraphJob() {
	for (HandlerList::iterator i = handlers.begin(); i != handlers.end(); ++i) {
		ComponentHandler * handler = *i;
		delete handler;
	}
	handlers.clear();
	delete component;
	delete thread;
}

void RunLinkGraphJob(void * j) {
	LinkGraphJob * job = (LinkGraphJob *)j;
	job->Run();
}

void Path::Fork(Path * base, float cap, float dist) {
	capacity = min(base->capacity, cap);
	distance = base->distance + dist;
	assert(distance > 0);
	if (parent != base) {
		if (parent != NULL) {
			parent->num_children--;
		}
		parent = base;
		parent->num_children++;
	}
}

void Path::AddFlow(float f, Component * graph) {
	flow +=f;
	graph->GetNode(node).paths.insert(this);
	if (parent != NULL) {
		parent->AddFlow(f, graph);
	}
}

void Path::UnFork() {
	if (parent != NULL) {
		parent->num_children--;
		parent = NULL;
	}
}

Path::Path(NodeID n, bool source)  :
	distance(source ? 0 : std::numeric_limits<float>::max()),
	capacity(source ? std::numeric_limits<float>::max() : 0),
	flow(0), node(n), num_children(0), parent(NULL)
{}

void LinkGraphJob::SpawnThread(CargoID cargo) {
	AddHandler(new DemandCalculator(cargo));
	AddHandler(new MultiCommodityFlow());
	if (!ThreadObject::New(&(RunLinkGraphJob), this, &thread)) {
		thread = NULL;
		// Of course this will hang a bit.
		// On the other hand, if you want to play games which make this hang noticably
		// on a platform without threads then you'll probably get other problems first.
		// OK:
		// If someone comes and tells me that this hangs for him/her, I'll implement a
		// smaller grained "Step" method for all handlers and add some more ticks where
		// "Step" is called. No problem in principle.
		RunLinkGraphJob(this);
	}
}

LinkGraphJob::LinkGraphJob(Component * c) :
	thread(NULL),
	join_time(_tick_counter + _settings_game.economy.linkgraph_recalc_interval * DAY_TICKS),
	component(c)
{}

LinkGraphJob::LinkGraphJob(Component * c, uint join) :
	thread(NULL),
	join_time(join),
	component(c)
{}

void LinkGraph::Clear() {
	jobs.clear();
	InitColours();
	current_colour = 0;
	current_station = 0;
}

void InitializeLinkGraphs() {
	InitializeDemands();
	for (CargoID c = CT_BEGIN; c != CT_END; ++c) _link_graphs[c].Clear();
}

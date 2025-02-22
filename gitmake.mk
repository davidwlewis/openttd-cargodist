#!/usr/bin/make -f

VPATH=.:./.git/refs/heads/

gitmake-all: cd
	touch gitmake-all
	git checkout patches
	mv patches/current/*.diff current/
	mv patches/current/TRUNK_VERSION.txt current/ || true
	mv patches/current/incremental/* current/incremental
	mv patches/incremental/* incremental/
	mv patches/*.diff .
	git add *.diff
	git add current
	git add incremental
	git commit --allow-empty -m "patches for version `cat current/TRUNK_VERSION.txt`"
	git checkout gitmake

check: clean gitmake-origin
	echo "check OK"

clean:
	rm -f gitmake-origin
	rm -rf patches
	mkdir -p patches/current/incremental
	mkdir -p patches/incremental

%:
	git checkout ${@}-tmp
	for branch in $(^F); do git merge $${branch}; done 
	git checkout $@
	git merge ${@}-tmp
	[ ! -e gitmake-build ] || make -j`cat gitmake-build`
	./make_diffs

gitmake-origin:
	git fetch git://git.openttd.org/openttd/trunk.git master:master
	git log master | grep -m 1 '(svn r[0-9]*)' | awk '{print $$2}' | sed -e 's/)//' > gitmake-origin
	cp gitmake-origin patches/current/TRUNK_VERSION.txt
	git diff --numstat master gitmake | grep -v .gitignore | grep -v gitmake | grep -v make_diffs | grep -v whitespace && touch gitmake-origin || touch -t 197001010100 gitmake-origin

gitmake: gitmake-origin
	git checkout $@
	git merge master
	[ ! -e gitmake-build ] || make -j`cat gitmake-build`

ext-rating: cargomap

station-gui: cargomap

texteff: gitmake 

flowmapping-core: mcf

mcf: demands

demands: components

components: capacities

capacities: moving-average

selfaware-stationcargo: gitmake 

cargomap: flowmapping-core texteff multimap reservation selfaware-stationcargo

reservation: gitmake

multimap: gitmake 

moving-average: gitmake 

cd: viewport-overlay smallmap-overlay ext-rating station-gui

linkgraph-overlay: cargomap

overlay-legend: linkgraph-overlay

smallmap-refactor: gitmake

smallmap-overlay: linkgraph-overlay smallmap-refactor

viewport-overlay: overlay-legend

push: master gitmake patches cd ext-rating station-gui flowmapping-core mcf demands components capacities texteff cargomap multimap reservation moving-average selfaware-stationcargo linkgraph-overlay smallmap-refactor smallmap-overlay viewport-overlay overlay-legend
	git push github $(^F)


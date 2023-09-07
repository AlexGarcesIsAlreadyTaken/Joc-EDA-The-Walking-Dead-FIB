#include "Player.hh"


/**
 * Write the name of your player and save this file
 * with the same name and .cc extension.
 */
#define PLAYER_NAME Machinim4

#define MAX_SEARCHERS 6	//Max # of units will chase the same food
#define MAX_DISTANCE 50	//Max distance we will BFS

#define FIRST 0	//Used to assalt enemies if they are at distance 1
#define SECOND 1	//Used to kill walkers or to get food
#define LAST 3 //Used to assalt enemy if they are diagonal to me


struct PLAYER_NAME : public Player {

  /**
   * Factory: returns a new instance of this class.
   * Do not modify this function.
   */
  static Player* factory () {
    return new PLAYER_NAME;
  }

  /**
   * Types and attributes for your player can be defined here.
   */

	enum Object {
		Wall,
		Enemy,
		Walker,
		Ally,
		Corpse,
		Food,
		Square, //Cells that I don't own
		Nothing,	//Nothing of my interest (my cells)
		InterestObject
	};

	struct Movement {
		int distance;
		Dir dir;
		Pos pos;
	};

	typedef vector<Object> row;
	typedef vector<row> matrix;
	
	typedef pair<int, Dir> mov;
	typedef pair<int, mov> priority;
	typedef priority_queue<priority, vector<priority>, greater<priority> > p_q;

	typedef pair<int, int> dist_id;
	typedef priority_queue<dist_id, vector<dist_id>, greater<dist_id> > searchers_list;

	matrix board;	//Board of the game
	vector<Pos> food_pos;	//Vector with the food positions at the current round
	vector<Pos> walker_pos; //Vector with the walkers positions at the current round
	vector<Pos> corpse_pos; //Vector with the corpse positions at the current round
	vector<Pos> enemy_pos;
	vector<Pos> interest_pos;
	set<int> units_moved;	//Set with the IDs of all my units that has decided his move
												//Not to move is also a move ;)
	set<int> last_moved;
	
	p_q priority_movements;	//p_q with the movements ordered by priority of movement
	const vector<Dir> Dirs = {Up, Right, Down, Left}; //All the moves a unit can do
	int SEARCHERS = MAX_SEARCHERS;
	int DISTANCE = MAX_DISTANCE;

	/**
	 * Functions and methods
	 */
	
	// Returns true if o is in position p, false otherwise
	bool object_in(const Pos& p, const Object& o) {
		return pos_ok(p) and board[p.i][p.j] == o;
	}
	
	//Returns true if is in position p any of the objects in O
	bool object_in(const Pos& p, const vector<Object>& O) {
		for (Object o : O) if (object_in(p, o)) return true;
		return false;
	}

	bool is_wall(const Pos& p) {
		return pos_ok(p) and cell(p).type == Waste;
	}
	
	// Returns true if a unit can move to that position 
	bool can_move(const Pos& p) {
		return pos_ok(p) and not object_in(p, Wall);
	}

	bool object_crossed(const Pos& p, const Object& o) {
		return	object_in(p+Up, o) or object_in(p+Down, o) or
						object_in(p+Right, o) or object_in(p+Left, o);
	}
	
	//Returns true if there are Objects o adjacents to p
	bool object_adjacent(const Pos& p, const Object& o) {
		return	object_in(p+Up, o) or object_in(p+Down, o)
						or object_in(p+Right, o) or object_in(p+Left, o)
						or object_in(p+DR, o) or object_in(p+RU, o)
						or object_in(p+UL, o) or object_in(p+LD, o); 
	}


	bool object_adjacent(const Pos& p, const Object& o, const Dir& D) {
		vector<Dir> allDirs = {Down, DR, Right, RU, Up, UL, Left, LD};
		for (int i = 0; i < 8; ++i) {
			Dir d = allDirs[i];
			if (d != D and object_in(p+d, o)) return true;
		}
		return false;
	}
	//id  is the id of an Alive unit
	//Return true  if id hasn't benn beaten
	inline bool healthy(int id) {
		return unit(id).rounds_for_zombie == -1;
	}

	// Returns true if it's secure for id not to move (not Walkers near or he is going to turn
	// into a Walker)
	bool secure(int id) {
		return not healthy(id) or not object_adjacent(unit(id).pos, Walker);
	}
	
	//Returns true if it's worth the wait
	//If it's not gonna turn into a Walker, it's worth
	//If it's gonna turn:
	//	There's a corpse near and he can kill him AGAIN when it turns into a walker
	bool worth_the_wait(int id, const Pos& p) {
		if (healthy(id)) return true; //I'm not  gonna turn into a zombie
		int life_time = unit(id).rounds_for_zombie;
		Cell c = cell(p);
		Unit u = unit(c.id);
		if (u.type == Dead) return life_time < u.rounds_for_zombie;
		return false;
	}

	//Returns true if id is can recorre distance before turning into a Walker
	inline bool worth_the_wait(int id, int distance) {
		int lifetime = unit(id).rounds_for_zombie;
		return lifetime == -1 or distance < lifetime;
	}
	
	//Returns true if there's a corridor in p
	//Used when there's a unit chasing food
	//If it's a corridor I just need one unit to go
	bool corridor(const Pos& p) {
		int n = 0;
		for (int i = 0; i < 4; ++i) if (can_move(p+Dirs[i])) ++n;
		return n == 2;
	}
	
	//Returns the inverse Dir to d: {Up -> Down}
	Dir inverse(const Dir& d) {
		if (d == Up) return Down;
		if (d == Right) return Left;
		if (d == Down) return Up;
		return Right;
	}
	
	//Returns the probability I have to win in an encounter against enemy
	float win_rate(int enemy) {
		int my_strength = strength(me());
		int	his_strength = strength(enemy); 
		if (my_strength == 0 and his_strength == 0) return 0.5;
		return float (my_strength)/(my_strength+his_strength);
	}

	//Returns the distance to the first o element starting from p with a max distance of max_dist
	int object_at_distance(int max_dist, Pos& P, const Object& o) {
		Pos p = P;
		priority_queue<pair<int, Pos>, vector<pair<int, Pos> >, greater<pair<int, Pos> > > Q;
		Q.push(make_pair(0, P));
		while (not Q.empty()) {
			int dist = Q.top().first;
			P = Q.top().second; Q.pop();
			if (dist >= max_dist) return -1;
			if (object_crossed(p, o)) return dist+1;
			if (can_move(p+Right)) Q.push(make_pair(dist+1, p+Right));
			if (can_move(p+Left)) Q.push(make_pair(dist+1, p+Left));
			if (can_move(p+Down)) Q.push(make_pair(dist+1, p+Down));
			if (can_move(p+Up)) Q.push(make_pair(dist+1, p+Up));
		}
		P = p;
		return -1;
	}
	
	//Returns the minimum distance to o from P in a max distance dist
	int distance(int dist, Pos& P, const Object& o) {
		return object_at_distance(dist, P, o);
	}

	//Put in Q the pairs <distance, id> of each element in M
	//The nature of Q orders automatically the values depending on their distance
	void order_by_distance(searchers_list& Q, const map<int, Movement>& M) {
		for (auto it = M.begin(); it != M.end(); ++it) {
			int id = it->first; int distance = it->second.distance;
			Q.push(make_pair(distance, id));
	}	}

	//"Moves" (put id in priority_movement) to the nearest object in O
	void go_to(int id, const vector<Object>& O) {
		if (units_moved.find(id) != units_moved.end()) return;
		
		queue<pair<Pos, Movement>> Q;
		vector<vector<bool> > visited(board_rows(), vector<bool>(board_cols(), false));
		
		Pos p = unit(id).pos;
		visited[p.i][p.j] = true;
		vector<int> v = random_permutation(4);
		for (int i = 0; i < 4; ++i) {
			Dir D = Dirs[v[i]]; Pos aux = p+D;
			if (can_move(aux)) {
				if (object_in(aux, O)) {
					units_moved.insert(id);
					priority_movements.push(make_pair(SECOND, make_pair(id, D)));
					return;
				}
				if (not object_adjacent(aux, Walker)) Q.push(make_pair(aux, Movement {1, D}));
				visited[aux.i][aux.j] = true;
		}	}

		while (not Q.empty()) {
			p = Q.front().first;
			int distance = Q.front().second.distance + 1;
			Dir D = Q.front().second.dir;	Q.pop();
			v = random_permutation(4);
			for (int i = 0; i < 4; ++i) {
				Dir d = Dirs[v[i]]; Pos aux = p+d;
				
				if (can_move(aux) and not visited[aux.i][aux.j] and distance <= DISTANCE) {
					if (object_in(aux, O)) {
						if ( not object_in(aux, {Enemy, Corpse})
								or (object_in(aux, Enemy) and win_rate(unit(cell(aux).id).player) > 0.5)
								or (object_in(aux, Corpse) and distance < 5)) {
						units_moved.insert(id);
						priority_movements.push(make_pair(SECOND, make_pair(id, D)));
						return;
				}	}
					Q.push(make_pair(aux, Movement{distance, D}));
					visited[aux.i][aux.j] = true;
	}	}	}	}
	
	//Finds the nearest units
	void bfs_find_units(const Pos& P, map<int, Movement>& M) {
		if (object_in(P, Wall)) return;
		int interest = 0;
		if (board[P.i][P.j] == Food) interest = 0;
		if (board[P.i][P.j] == Walker) interest = 8;
		if (board[P.i][P.j] == Enemy) interest = 4;
		if (board[P.i][P.j] == Corpse) interest = 10;
		queue<pair<Pos, int>> Q;
		vector<vector<bool> > visited(board_rows(), vector<bool>(board_cols(), false));
		int units_found = 0; //The # of units found during this call
		
		vector<int> v = random_permutation(4);
		for (int i = 0; i < 4; ++i) {
			Dir d = Dirs[v[i]];
			if (object_in(P+d, Ally) and not object_adjacent(P, Walker)) {
				int id = cell(P+d).id;
				if (units_moved.find(id) == units_moved.end()) {
					Movement m = {1, inverse(d), P};
					if (M.find(id) == M.end()) M.insert(make_pair(id, m));
					else M[id] = m;
					//Not gonna follow this path if it's a corridor (waste of time)
					if (not corridor(P+d)) Q.push(make_pair(P+d, 1));
					
					++units_found; if (units_found > SEARCHERS) return;
			}	}
			else if (pos_ok(P+d) and not is_wall(P+d)) Q.push(make_pair(P+d, 1));
			if (pos_ok(P+d)) visited[(P+d).i][(P+d).j] = true;
		}
		visited[P.i][P.j] = true;

		
		while (not Q.empty()) {
			Pos p = Q.front().first; int distance = Q.front().second + 1; Q.pop();
			v = random_permutation(4);
			for (int z = 0; z < 4; ++z) {
				Dir d = Dirs[v[z]]; 
				Pos aux = p+d;
				int i = aux.i; int j = aux.j;
				if (distance <= DISTANCE and can_move(aux) and not visited[i][j]) {
					visited[i][j] = true;
					int id = cell(aux).id;
					if (object_in(aux, Ally) and not object_adjacent(p, Walker)) {
						if (units_moved.find(id) == units_moved.end()) {
							int D = distance; if (healthy(id)) D += interest;
							Movement m = {D, inverse(d), P};
							if (M.find(id) == M.end()) M.insert(make_pair(id, m));
							else if (M[id].distance > D) M[id] = m;

							//Not gonna follow this path if it's a corridor (waste of time)
							if (not corridor(aux)) Q.push(make_pair(aux, distance));
							
							++units_found; if (units_found > SEARCHERS) return;
					}	}
					else Q.push(make_pair(aux, distance));
	} } } }
	
	//Finds the nearest units to each food. 
	//After calling bfs_find_units, M will store all the units with the DIR to their nearest food
	//However it will only chase the food that is nearest
	void move_nearest_unit() {
		map<int, Movement> M;
		for (Pos p : interest_pos) bfs_find_units(p, M);

		set<Pos> obj_assigned;
		searchers_list Q; order_by_distance(Q, M);
		while (not Q.empty()) { //Takes the unit with minimum distance to an object and it's the only
			int id = Q.top().second; Q.pop();
			Movement m = M[id];
			if (obj_assigned.find(m.pos) == obj_assigned.end()) {
			obj_assigned.insert(m.pos);
			units_moved.insert(id);
			priority_movements.push(make_pair(SECOND, make_pair(id, m.dir)));}
	}	}

	//looks if it's a menace id can directly attack and do it if its possible
	void attack_nearest(int id) {
		if (units_moved.find(id) != units_moved.end()) return;
		
		Pos p = unit(id).pos;
		vector<int> v = random_permutation(4);
		//FIRST, I must attack first in order to get most rate of win
		for (int i = 0; i < 4; ++i) {
			Dir d = Dirs[v[i]];
			if (object_in(p+d, Enemy)) {
				priority_movements.push(make_pair(FIRST, make_pair(id, d)));
				units_moved.insert(id);	return;
		} }

		
		//MID PRIORTY, Walkers moves the last so i'm not gonna take it seriously
		v = random_permutation(4);
		for (int i = 0; i < 4; ++i) {
			Dir d = Dirs[v[i]];
			if (object_in(p+d, Walker) 
					and not (object_adjacent(p, Walker, d) and healthy(id))) {
				priority_movements.push(make_pair(SECOND, make_pair(id, d)));
				board[(p+d).i][(p+d).j] = Wall;
				units_moved.insert(id);	return;
		} }

		if (object_crossed(p, Corpse)) return; //If we are camping a corpse we only wanna attack if we are not moving at all
		
		v = random_permutation(4);
		for (int i = 0; i < 4; ++i) {
			Dir d = Dirs[v[i]];
			if (object_in(p+d+d, Enemy) and not is_wall(p+d) and not (healthy(id) and object_adjacent(p+d, Walker))) {
				priority_movements.push(make_pair(LAST, make_pair(id, d)));
				units_moved.insert(id);	return;
		} }


		//LAST, I'm gonna wait, if he moves to that position I can attack him directly
		//Priorize position if it's Food in there (more probability Enemy will go there)
		int r = random(0, 1);
		if (object_in(p+DR, Enemy)) {
			if (can_move(p+Down) and object_crossed(p+Down, Food)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Down)));
				last_moved.insert(id);
				units_moved.insert(id);	return;
			}
			else if (can_move(p+Right) and (object_crossed(p+Right, Food))) {
				priority_movements.push(make_pair(LAST, make_pair(id, Right)));
				last_moved.insert(id);
				units_moved.insert(id); return;
			}
			else if (r == 0 and can_move(p+Right) and not object_adjacent(p+Right, Walker)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Right)));
				last_moved.insert(id);
				units_moved.insert(id); return;
			}
			else if (can_move(p+Down) and not object_adjacent(p+Down, Walker)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Down)));
				last_moved.insert(id);
				units_moved.insert(id);	return;
		} }
		if (object_in(p+RU, Enemy)) {
			if (can_move(p+Up) and object_crossed(p+Up, Food)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Up)));
				last_moved.insert(id);
				units_moved.insert(id);	return;
			}
			else if (can_move(p+Right) and object_crossed(p+Right, Food)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Right)));
				last_moved.insert(id);
				units_moved.insert(id);	return;
			}
			else if (r == 0 and can_move(p+Up) and not object_adjacent(p+Up, Walker)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Up)));
				last_moved.insert(id);
				units_moved.insert(id); return;
			}
			else if (can_move(p+Right) and not object_adjacent(p+Right, Walker)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Right)));
				last_moved.insert(id);
				units_moved.insert(id);	return;
		} }
		if (object_in(p+UL, Enemy)) {
			if (can_move(p+Up) and object_crossed(p+Up, Food)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Up)));
				last_moved.insert(id);
				units_moved.insert(id);	return;
			}
			else if (can_move(p+Left) and object_crossed(p+Left, Food)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Left)));
				last_moved.insert(id);
				units_moved.insert(id);	return;
			}
			else if (r == 0 and can_move(p+Up) and not object_adjacent(p+Up, Walker)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Up)));
				last_moved.insert(id);
				units_moved.insert(id); return;
			}
			else if (can_move(p+Left) and not object_adjacent(p+Left, Walker)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Left)));
				last_moved.insert(id);
				units_moved.insert(id);	return;
		} }
		if (object_in(p+LD, Enemy)) {
			if (can_move(p+Down) and object_crossed(p+Down, Food)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Down)));
				last_moved.insert(id);
				units_moved.insert(id);	return;
			}
			else if (can_move(p+Left) and object_crossed(p+Left, Food)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Left)));
				last_moved.insert(id);
				units_moved.insert(id);	return;
			}
			else if (r == 0 and can_move(p+Left) and not object_adjacent(p+Left, Walker)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Left)));
				last_moved.insert(id);
				units_moved.insert(id); return;
			}
			else if (can_move(p+Down) and not object_adjacent(p+Down, Walker)) {
				priority_movements.push(make_pair(LAST, make_pair(id, Down)));
				last_moved.insert(id);
				units_moved.insert(id);	return;
		} }
		//I'm evaluating Enemies first because they gave more points and also they are intelligent
		//Walkers only goes to neares Alives
	}
	
	// id decides not to move if it will give him beneficts at future
	void wait_if_worth(int id) {
		if (units_moved.find(id) != units_moved.end()) return;
		Pos p = unit(id).pos;

		//CASE 1: Wait Corpse to turn into a zombie
		for (int i = 0; i < 4; ++i) {
			Pos aux = p+Dirs[i];
			if (object_in(aux, Corpse) and worth_the_wait(id, aux)) {
				//if a zombie can attack me and I'm not beaten it's better to run to an adjacent position to
				//Corpse that is also secure, if there's not the unit is sacrificed
				if (object_adjacent(p, Walker) and healthy(id)) {
					for (int j = 0; j < 4; ++j) {
						if (j != i and not object_adjacent(p+Dirs[j], Walker) and object_adjacent(p+Dirs[j], Corpse)) {
							priority_movements.push(make_pair(SECOND, make_pair(id, Dirs[j])));
							units_moved.insert(id);
							return;
				}	}	}
				else units_moved.insert(id);
				board[aux.i][aux.j] = Wall;
		}	}
		//CASE 2: Distance 2 to Walker, {There's only one or I have no time} <- not implemented
		//Waits if finds 
		/*for (int i = 0; i < 4; ++i) {
			if (object_in(p+Dirs[i]+Dirs[i], Walker) and not (healthy(id) and object_adjacent(p, Walker))) {
				units_moved.insert(id);
		} }*/
		//CASE 3: Enemy at distance 3
		if (distance(3, p, Enemy) == 3 and secure(id) and not object_crossed(p, Food)) {
			units_moved.insert(id);
			board[p.i][p.j] = Wall;
		}
		if (units_moved.find(id) != units_moved.end())
			priority_movements.push(make_pair(SECOND, make_pair(id, UL)));
	}

	
	//Move all the IDs in priority movement ordered by priority
	void move_units() {
		while (not priority_movements.empty()) {
			mov m = priority_movements.top().second;
			move(m.first, m.second);
			priority_movements.pop();
	}	}

	//Writes on board what is in each position {Wall, Walker, Enemy, Food, etc.}
	void write_board() {
		board = matrix(board_rows(), row(board_cols(), Nothing));
		for (int i = 0; i < board_rows(); ++i) {
			for (int j = 0; j < board_cols(); ++j) {
				Pos p = Pos(i, j);
				Cell c = cell(p);
				int id = c.id;
				if (c.type == Waste) board[i][j] = Wall;
				else if (c.food) {
					board[i][j] = Food;
					food_pos.push_back(p);
					interest_pos.push_back(p);
				}
				else if (id == -1) {if (c.owner != me()) board[i][j] = Square;}
				else if (unit(id).type == Zombie) {
					board[i][j] = Walker;
					walker_pos.push_back(p);
					interest_pos.push_back(p);
				}
				else if (unit(id).type == Dead)	{
					board[i][j] = Corpse;
					corpse_pos.push_back(p);
					interest_pos.push_back(p);
				}
				else if (unit(id).type == Alive) {
					if(unit(id).player == me()) board[i][j] = Ally;
					else {
						board[i][j] = Enemy;
						enemy_pos.push_back(p);
						interest_pos.push_back(p);
					}
	}	} }	}
	
	void check_corpses() {
		for (Pos p : corpse_pos) if (object_crossed(p, Enemy) and not object_crossed(p, Ally)) board[p.i][p.j] = Wall;
	}

	void check_walkers() {
		for (Pos p : walker_pos) {
			vector<Dir> allDirs = {Down, DR, Right, RU, Up, UL, Left, LD};
			for (int i = 0; i < 8; ++i) {
				Dir d = allDirs[i];
				Pos aux = p+d;
				if (pos_ok(aux) and not object_in(aux, {Walker, Enemy, Ally, Corpse, Food}))
					board[aux.i][aux.j] = Wall;
	}	}	}

	void check_enemies() {
		for (Pos p : enemy_pos)
		for (int i = 0; i < 4; ++i) {
			Dir d = Dirs[i];
			Pos aux = p+d;
			if (pos_ok(aux) and not object_in(aux, {Walker, Enemy, Ally, Corpse, Food}))
				board[aux.i][aux.j] = Wall;
	}	}

	void reboot() {
		interest_pos.clear();
		last_moved.clear();
		food_pos.clear();
		walker_pos.clear();
		corpse_pos.clear();
		enemy_pos.clear();
		units_moved.clear();
		board.clear();
		write_board();
		check_corpses();
		check_enemies();
		check_walkers();
	}

	void move_random(int id) {
		if (units_moved.find(id) != units_moved.end()) return;
		Pos p = unit(id).pos;
		vector<int> v = random_permutation(4);
		for (int z = 0; z < 4; ++z) {
			Dir d = Dirs[v[z]];
			if (pos_ok(p+d) and not is_wall(p+d) and not object_in(p+d, {Corpse})) {
				priority_movements.push(make_pair(SECOND, make_pair(id, d)));
				units_moved.insert(id);
	}	}	}
	
	/**
   * Play method, invoked once per each round.
   */

  virtual void play () {
		reboot();
		vector<int> my_units = alive_units(me());
		
		for (int id : my_units) attack_nearest(id);
		for (int id : my_units) wait_if_worth(id);
		DISTANCE = MAX_DISTANCE;
		move_nearest_unit();
		DISTANCE = 1000;
		for (int id : my_units) go_to(id, {Square});
		for (int id : my_units) move_random(id);
		
		move_units();
  }

};


/**
 * Do not modify the following line.
 */
RegisterPlayer(PLAYER_NAME);

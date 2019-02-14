#ifndef SNZI_H
#define SNZI_H

struct snzi_node {
	bool is_root;

	union {
		struct {
			struct snzi_node *parent;
			int var;
		} leaf;
		struct {
			int state;
			int var;
		} root;
	} x;

	char pad[64];
};

struct snzi {
	struct snzi_node root;
	struct snzi_node nodes[NR_CPUS];
};

static int root_encode(short c, bool a, short v) {
	unsigned int temp = 0;
	temp |= (unsigned int)c;
	temp |= (unsigned int)(a ? 0x8000 : 0);
	temp |= ((unsigned int)v) << 16;

	return (int)temp;
}

static void root_decode(int value, short *c, bool *a, short *v) {
	unsigned int temp = (unsigned int)value;
	*c = (short)(temp & 0x7FFF);
	*a = (temp & 0x8000) > 0;
	*v = (short)(temp >> 16);
}

static int leaf_encode(short c, short v) {
	unsigned int temp = 0;
	temp |= (unsigned int)c;
	temp |= ((unsigned int)v) << 16;

	return (int)temp;
}

void root_init(struct snzi_node *);
void leaf_init(struct snzi_node *, struct snzi_node *);

bool root_query(struct snzi_node *);
bool leaf_query(struct snzi_node *);
bool node_query(struct snzi_node *);

void root_arrive(struct snzi_node *);
void leaf_arrive(struct snzi_node *);
void node_arrive(struct snzi_node *);

void root_depart(struct snzi_node *);
void leaf_depart(struct snzi_node *);
void node_depart(struct snzi_node *);

static void leaf_decode(int value, short *c, short *v) {
	unsigned int temp = (unsigned int)value;
	*c = (short)(temp & 0xFFFF);
	*v = (short)(temp >> 16);
}

void root_arrive(struct snzi_node *node) {
	int temp, x = 0;
	short c, v;
	bool a;

	do {
		x = node->x.root.var;
		root_decode(x, &c, &a, &v);

		if (c == 0)
			temp = root_encode(1, true, (short)(v + 1));
		else
			temp = root_encode((short)(c + 1), a, v);

	} while (cmpxchg(&node->x.root.var, x, temp) != x);

	root_decode(temp, &c, &a, &v);

	if (a) {
		while (true) {
			int i = node->x.root.state;
			int newi = (i & 0x7FFFFFFF) + 1;
			newi = (int)(((unsigned int)newi) | 0x80000000);

			if (cmpxchg(&node->x.root.state, i, newi) == i) break;
		}

		cmpxchg(&node->x.root.var, root_encode(c, false, v), temp);
	}
}

void root_depart(struct snzi_node *node) {
	while (true) {
		int x = node->x.root.var;
		short c, v;
		bool a;

		root_decode(x, &c, &a, &v);

		if (cmpxchg(&node->x.root.var, x,
			    root_encode((short)(c - 1), false, v)) == x) {
			if (c >= 2) return;

			while (true) {
				int i = node->x.root.state;
				int newi;
				if (((short)(node->x.root.var >> 16)) != v)
					return;

				newi = (i & 0x7FFFFFFF) + 1;

				if (cmpxchg(&node->x.root.state, i, newi) == i)
					return;
			}
		}
	}
}

bool root_query(struct snzi_node *node) {
	return (node->x.root.state & 0x80000000) > 0;
}

void leaf_arrive(struct snzi_node *node) {
	bool succ = false;
	int undoArr = 0;
	int i;

	while (!succ) {
		int x = node->x.leaf.var;
		short c, v;

		leaf_decode(x, &c, &v);

		if (c >= 1) {
			if (cmpxchg(&node->x.leaf.var, x,
				    leaf_encode((short)(c + 1), v)) == x)
				break;
		}

		if (c == 0) {
			int temp = leaf_encode(-1, (short)(v + 1));
			if (cmpxchg(&node->x.leaf.var, x, temp) == x) {
				succ = true;
				c = -1;
				v += 1;
				x = temp;
			}
		}

		if (c == -1) {
                        if (undoArr == 2) {
			        if (cmpxchg(&node->x.leaf.var, x,
                                         leaf_encode(1, v)) == x)
                                        undoArr--;
                        } else {
			        node_arrive(node->x.leaf.parent);
			        if (cmpxchg(&node->x.leaf.var, x,
                                        leaf_encode(1, v)) != x)
				        undoArr++;
                        }
		}
	}

	for (i = 0; i < undoArr; i++) node_depart(node->x.leaf.parent);
}

void leaf_depart(struct snzi_node *node) {
	while (true) {
		int x = node->x.leaf.var;
		short c, v;

		leaf_decode(x, &c, &v);

		if (cmpxchg(&node->x.leaf.var, x,
			    leaf_encode((short)(c - 1), v)) == x) {
			if (c == 1) node_depart(node->x.leaf.parent);
			return;
		}
	}
}

bool leaf_query(struct snzi_node *node) {
	return node_query(node->x.leaf.parent);
}

void node_arrive(struct snzi_node *node) {
	if (node->is_root)
		root_arrive(node);
	else
		leaf_arrive(node);
}

void node_depart(struct snzi_node *node) {
	if (node->is_root)
		root_depart(node);
	else
		leaf_depart(node);
}

bool node_query(struct snzi_node *node) {
	if (node->is_root) return root_query(node);
	return leaf_query(node);
}

void root_init(struct snzi_node *root) {
	root->is_root = true;
	root->x.root.var = 0;
	root->x.root.state = 0;
}

void leaf_init(struct snzi_node *leaf, struct snzi_node *parent) {
	leaf->is_root = false;
	leaf->x.leaf.parent = parent;
	leaf->x.leaf.var = 0;
}

void snzi_init(struct snzi *obj) {
	int i = 0;

	root_init(&obj->root);
	for (i = 0; i < NR_CPUS; i++) leaf_init(&obj->nodes[i], &obj->root);
}

void snzi_inc(struct snzi *obj, unsigned int tid) {
	struct snzi_node *node = &obj->nodes[tid];
	node_arrive(node);
}

void snzi_dec(struct snzi *obj, unsigned int tid) {
	struct snzi_node *node = &obj->nodes[tid];
	node_depart(node);
}

bool snzi_query(struct snzi *obj) { return node_query(&obj->root); }

#endif

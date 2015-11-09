#ifndef CRAB_PT_GRAPH_HPP
#define CRAB_PT_GRAPH_HPP
#include <vector>
#include <crab/domains/patricia_trees.hpp>

/* Patricia-tree backed sparse weighted graph.
 * Trades some time penalty for much lower memory consumption.
 */
namespace ikos {

template <class Weight>
class PtGraph : public writeable {
  public:
    typedef Weight Wt;
    typedef PtGraph<Wt> graph_t;

    typedef unsigned int vert_id;
    
    class vert_idx {
    public:
      vert_idx(vert_id _v)
        : v(_v)
      { }
      index_t index(void) const { return (index_t) v; }

      void write(std::ostream& o) {
        o << v;
      }

      vert_id v;
    };

    typedef patricia_tree_set<vert_idx> pred_t;
    typedef patricia_tree<vert_idx, Wt> succ_t;

    PtGraph()
      : edge_count(0), _succs(), _preds(), is_free(), free_id()
    {

    }

    template<class Wo>
    PtGraph(const PtGraph<Wo>& o)
      : edge_count(0)
    {
      for(vert_id v : o.verts())
        for(vert_id d : o.succs(v))
          add_edge(v, o.edge_val(v, d), d);
    }

    PtGraph(const PtGraph<Wt>& o)
      : edge_count(o.edge_count), _succs(o._succs), _preds(o._preds),
        is_free(o.is_free), free_id(o.free_id)
    { }

    PtGraph(PtGraph<Wt>&& o)
      : edge_count(o.edge_count), _succs(std::move(o._succs)), _preds(std::move(o._preds)),
        is_free(std::move(o.is_free)), free_id(std::move(o.free_id))
    {
      o.edge_count = 0;
    }

    PtGraph& operator=(const PtGraph<Wt>& o)
    {
      if((&o) == this)
        return *this;  
      
      edge_count = o.edge_count;
      _succs = o._succs; 
      _preds = o._preds;
      is_free = o.is_free;
      free_id = o.free_id;

      return *this;
    }

    PtGraph& operator=(PtGraph<Wt>&& o)
    {
      edge_count = o.edge_count;
      _succs = std::move(o._succs);
      _preds = std::move(o._preds);
      is_free = std::move(o.is_free);
      free_id = std::move(o.free_id);
      
      return *this;
    }

    void check_adjs(void)
    {
      for(vert_id v : verts())
      {
        assert(succs(v).size() <= _succs.size());
        for(vert_id s : succs(v))
        {
          assert(s < _succs.size());
          assert(preds(s).mem(v));
        }

        assert(preds(v).size() <= _succs.size());
        for(vert_id p : preds(v))
        {
          assert(p < _succs.size());
          assert(succs(p).mem(v));
        }
      }
    }

    ~PtGraph()
    { }

    // GKG: Can do this more efficiently
    template<class G> 
    static graph_t copy(G& g)
    {
      graph_t ret;
      ret.growTo(g.size());

      for(vert_id s : g.verts())
      {
        for(vert_id d : g.succs(s))
        {
          ret.add_edge(s, g.edge_val(s, d), d);
        }
      }

      return ret;
    }

    bool is_empty(void) const { return edge_count == 0; }

    vert_id new_vertex(void)
    {
      vert_id v;
      if(free_id.size() > 0)
      {
        v = free_id.back();
        assert(v < _succs.size());
        free_id.pop_back();
        is_free[v] = false;
      } else {
        v = is_free.size();
        _succs.push_back(succ_t());
        _preds.push_back(pred_t());
        is_free.push_back(false);
      }

      return v;
    }

    void forget(vert_id v)
    {
      assert(v < _succs.size());
      if(is_free[v])
        return;

      free_id.push_back(v); 
      is_free[v] = true;
      
      // Remove (s -> v) from preds.
      edge_count -= succs(v).size();
      for(vert_id d : succs(v))
        preds(d).remove(v);
      _succs[v].clear();

      // Remove (v -> p) from succs
      edge_count -= preds(v).size();
      for(vert_id p : preds(v))
        succs(p).remove(v);
      _preds[v].clear();
    }

    // Check whether an edge is live
    bool elem(vert_id x, vert_id y) {
      return succs(x).mem(y);
    }

    // GKG: No longer a ref
    Wt edge_val(vert_id x, vert_id y) {
      return succs(x).value(y);
    }

    // Precondition: elem(x, y) is true.
    Wt operator()(vert_id x, vert_id y) {
      // return mtx[max_sz*x + y];
      return succs(x).value(y);
    }

    void clear_edges(void) {
      edge_count = 0;
      for(vert_id v : verts())
      {
        _succs[v].clear();
        _preds[v].clear();
      }
    }

    void clear(void)
    {
      edge_count = 0;
      is_free.clear();
      free_id.clear();
      _succs.clear();
      _preds.clear();
    }

    // Number of allocated vertices
    int size(void) const {
      return is_free.size();
    }

    // Assumption: (x, y) not in mtx

    void add_edge(vert_id x, Wt wt, vert_id y)
    {
      succs(x).add(y, wt);
      preds(y).add(x);
      edge_count++;
    }

    void set_edge(vert_id s, Wt w, vert_id d)
    {
//      assert(s < size() && d < size());
      if(!elem(s, d))
        add_edge(s, w, d);
      else
        _succs[s].insert(vert_idx(d), w);
    }

    template<class Op>
    void update_edge(vert_id s, Wt w, vert_id d, Op& op)
    {
      if(elem(s, d))
      {
//        _succs[s].insert(vert_idx(d), w, op);
        _succs[s].insert(vert_idx(d), op.apply(edge_val(s, d), w));
//        edge_val(s, d) = op.apply(edge_val(s, d), w);
        return;
      }

      if(!op.default_is_absorbing())
        add_edge(s, w, d);
    }

    class vert_iterator {
    public:
      vert_iterator(vert_id _v, const vector<bool>& _is_free)
        : v(_v), is_free(_is_free)
      { }
      vert_id operator*(void) const { return v; }
      vert_iterator& operator++(void) { ++v; return *this; }
//      vert_iterator& operator--(void) { --v; return *this; }
      bool operator!=(const vert_iterator& o) {
        while(v < o.v && is_free[v])
          ++v;
        return v < o.v;
      }
    protected:
      vert_id v;
      const vector<bool>& is_free;
    };

    class vert_range {
    public:
      vert_range(const vector<bool>& _is_free)
        : is_free(_is_free)
      { }
      vert_iterator begin(void) const { return vert_iterator(0, is_free); }
      vert_iterator end(void) const { return vert_iterator(is_free.size(), is_free); }
    protected:
      const vector<bool>& is_free;
    };

    // FIXME: Verts currently iterates over free vertices,
    // as well as existing ones
    vert_range verts(void) const { return vert_range(is_free); }

    class pred_iterator {
    public:
      typedef typename pred_t::iterator ItP;
      typedef pred_iterator iter_t;

      pred_iterator(const ItP& _it)
        : it(_it)
      { }
      pred_iterator(void)
        : it()
      { }
      bool operator!=(const iter_t& o) {
        return it != o.it;
      }
      iter_t& operator++(void) { ++it; return *this; }
      vert_id operator*(void) const { return (*it).v; }
    protected:
      ItP it;
    };

    class succ_iterator {
    public:
      typedef typename succ_t::iterator ItS;
      typedef succ_iterator iter_t;
      succ_iterator(const ItS& _it)
        : it(_it)
      { }
      succ_iterator(void)
        : it()
      { }
      bool operator!=(const iter_t& o) {
        return it != o.it;
      }
      iter_t& operator++(void) { ++it; return *this; }
      vert_id operator*(void) const { return (*it).first.v; }
    protected:
      ItS it;      
    };

    class pred_range {
    public:
      typedef pred_iterator iterator;

      pred_range(pred_t& _p)
        : p(_p)
      { }
      iterator begin(void) const { return iterator(p.begin()); }
      iterator end(void) const { return iterator(p.end()); }
      size_t size(void) const { return p.size(); }

      bool mem(unsigned int v) const { return p[v]; }
      void add(unsigned int v) { p += v; }
      void remove(unsigned int v) { p -= v; }
      void clear() { p.clear(); }

    protected:
      pred_t& p;
    };

    class succ_range {
    public:
      typedef succ_iterator iterator;

      succ_range(succ_t& _p)
        : p(_p)
      { }
      iterator begin(void) const { return iterator(p.begin()); }
      iterator end(void) const { return iterator(p.end()); }
      size_t size(void) const { return p.size(); }

      bool mem(unsigned int v) const { return p.lookup(v); }
      void add(unsigned int v, const Wt& w) { p.insert(v, w); }
      Wt value(unsigned int v) { return *(p.lookup(v)); }
      void remove(unsigned int v) { p.remove(v); }
      void clear() { p.clear(); }

    protected:
      succ_t& p;
    };

//    typedef adj_range<pred_t, pred_iterator> pred_range;
//    typedef adj_range<succ_t, succ_iterator> succ_range;

    succ_range succs(vert_id v)
    {
      return succ_range(_succs[v]);
    }
    pred_range preds(vert_id v)
    {
      return pred_range(_preds[v]);
    }

    // growTo shouldn't be used after forget
    void growTo(unsigned int new_sz)
    {
      size_t sz = is_free.size();
      for(; sz < new_sz; sz++)
      {
        is_free.push_back(false);
        _preds.push_back(pred_t());
        _succs.push_back(succ_t());
      }
      assert(free_id.size() == 0);
    }

  protected:
    void write(std::ostream& o) {
      o << "[|";
      bool first = true;
      for(vert_id v = 0; v < _succs.size(); v++)
      {
        auto it = succs(v).begin();
        auto end = succs(v).end();

        if(it != end)
        {
          if(first)
            first = false;
          else
            o << ", ";

          o << "[v" << v << " -> ";
          o << "(" << edge_val(v, *it) << ":" << *it << ")";
          for(++it; it != end; ++it)
          {
            o << ", (" << edge_val(v, *it) << ":" << *it << ")";
          }
          o << "]";
        }
      }
      o << "|]";
    }

    unsigned int edge_count;

    vector<succ_t> _succs;
    vector<pred_t> _preds;

    std::vector<bool> is_free;
    std::vector<int> free_id;
};

}
#endif
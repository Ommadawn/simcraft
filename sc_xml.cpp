// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simcraft.h"

struct xml_parm_t
{
  std::string name_str;
  std::string value_str;
  xml_parm_t( const std::string& n, const std::string& v ) : name_str(n), value_str(v) {}
  const char* name() { return name_str.c_str(); }
};

struct xml_node_t
{
  std::string name_str;
  std::vector<xml_node_t*> children;
  std::vector<xml_parm_t> parameters;
  xml_node_t() {}
  xml_node_t( const std::string& n ) : name_str(n) {}
  const char* name() { return name_str.c_str(); }
  xml_parm_t* get_parm( const std::string& name_str )
  {
    int num_parms = parameters.size();
    for( int i=0; i < num_parms; i++ ) 
      if( name_str == parameters[ i ].name() )
	return &( parameters[ i ] );
    return 0;
  }
};

struct xml_cache_t
{
  std::string url;
  xml_node_t* node;
  xml_cache_t() : node(0) {}
  xml_cache_t( const std::string& u, xml_node_t* n ) : url(u), node(n) {}
};

static void* xml_mutex = 0;

namespace { // ANONYMOUS NAMESPACE =========================================

// Forward Declarations ====================================================

static int create_children( xml_node_t* root, const std::string& input, std::string::size_type& index );

// is_white_space ==========================================================

static bool is_white_space( char c )
{
  return( c == ' '  ||
	  c == '\t' ||
	  c == '\n' ||
	  c == '\r' );
}

// is_name_char ============================================================

static bool is_name_char( char c )
{
  if( isalpha( c ) ) return true;
  if( isdigit( c ) ) return true;
  return( c == '_' || c == '-' );
}

// parse_name ==============================================================

static bool parse_name( std::string&            name_str,
			const std::string&      input,
			std::string::size_type& index )
{
  name_str.clear();

  char c = input[ index ];

  if( ! is_name_char( c ) )
    return false;

  while( is_name_char( c ) )
  {
    name_str += c;
    c = input[ ++index ];
  }

  return true;
}

// create_parameter ========================================================

static void create_parameter( xml_node_t*             node,
			      const std::string&      input,
			      std::string::size_type& index )
{
  // required format:  name="value"

  std::string name_str;
  if( ! parse_name( name_str, input, index ) )
    return;
  
  assert( input[ index ] == '=' );
  index++;
  assert( input[ index ] == '"' );
  index++;

  std::string::size_type start = index;
  while( input[ index ] != '"' ) 
  { 
    assert( input[ index ] );
    index++;
  }
  std::string value_str = input.substr( start, index-start );
  index++;

  node -> parameters.push_back( xml_parm_t( name_str, value_str ) );
}

// create_node =============================================================

static xml_node_t* create_node( const std::string&      input,
				std::string::size_type& index )
{
  char c = input[ index ];
  if( c == '?' ) index++;

  std::string name_str;
  parse_name( name_str, input, index );
  assert( ! name_str.empty() );

  xml_node_t* node = new xml_node_t( name_str );

  while( input[ index ] == ' ' )
  {
    create_parameter( node, input, ++index );
  }

  c = input[ index ];

  if( c == '/' || c == '?' )
  {
    index += 2;
  }
  else if( c == '>' )
  {
    create_children( node, input, ++index );
  }
  else 
  {
    printf( "Unexpected character '%c' at index %d (%s)\n", c, (int) index, node -> name() );
    printf( "%s\n", input.c_str() );
    assert( false );
  }

  return node;
}

// create_children =========================================================

static int create_children( xml_node_t*             root,
			    const std::string&      input,
			    std::string::size_type& index )
{
  while( true )
  {
    while( is_white_space( input[ index ] ) ) index++;

    if( input[ index ] == '<' )
    {
      index++;
      
      if( input[ index ] == '/' )
      {
	std::string name_str;
	parse_name( name_str, input, ++index );
	assert( name_str == root -> name() );
	index++;
	break;
      }
      else
      {
	root -> children.push_back( create_node( input, index ) );
      }
    }
    else if( input[ index ] == '\0' )
    {
      break;
    }
    else
    {
      std::string::size_type start = index;
      while( input[ index ] && input[ index ] != '<' ) index++;
      root -> parameters.push_back( xml_parm_t( ".", input.substr( start, index-start ) ) );
    }
  }

  return root -> children.size();
}

// split_path ==============================================================

static xml_node_t* split_path( xml_node_t*        node,
			       std::string&       key,
			       const std::string& path )
{
  if( path.find( "/" ) == path.npos )
  {
    key = path;
  }
  else
  {
    std::vector<std::string> splits;
    int num_splits = util_t::string_split( splits, path, "/" );

    for( int i=0; i < num_splits-1; i++ )
    {
      node = xml_t::get_node( node, splits[ i ] );
      if( ! node ) return 0;
    }

    key = splits[ num_splits-1 ];
  }

  return node;
}

} // ANONYMOUS NAMESPACE ===================================================

// xml_t::download =========================================================

xml_node_t* xml_t::download( const std::string& url, 
			     const std::string& confirmation, 
			     int                throttle_seconds )
{
  thread_t::mutex_lock( xml_mutex );

  static std::vector<xml_cache_t> xml_cache;
  int size = xml_cache.size();

  xml_node_t* node = 0;

  for( int i=0; i < size && ! node; i++ )
    if( xml_cache[ i ].url == url )
      node = xml_cache[ i ].node;

  if( ! node )
  {
    std::string result;

    if( http_t::get( result, url, confirmation, throttle_seconds ) )
    {
      node = xml_t::create( result );

      if( node ) xml_cache.push_back( xml_cache_t( url, node ) );
    }  
  }

  thread_t::mutex_unlock( xml_mutex );

  return node;
}

// xml_t::create ===========================================================

xml_node_t* xml_t::create( const std::string& input )
{
  xml_node_t* root = new xml_node_t( "root" );

  std::string::size_type index=0;

  create_children( root, input, index );

  return root;
}

// xml_t::get_node =========================================================

xml_node_t* xml_t::get_node( xml_node_t*        root,
			     const std::string& name_str )
{
  if( name_str.empty() || name_str.size() == 0 || name_str == root -> name() ) 
    return root;

  int num_children = root -> children.size();
  for( int i=0; i < num_children; i++ )
  {
    xml_node_t* node = get_node( root -> children[ i ], name_str );
    if( node ) return node;
  }

  return 0;
}

// xml_t::get_nodes ========================================================

int xml_t::get_nodes( std::vector<xml_node_t*>& nodes,
		      xml_node_t*               root,
		      const std::string&        name_str )
{
  if( name_str.empty() || name_str.size() == 0 || name_str == root -> name() )
  {
    nodes.push_back( root );
  }
  else
  {
    int num_children = root -> children.size();
    for( int i=0; i < num_children; i++ )
    {
      get_nodes( nodes, root -> children[ i ], name_str );
    }
  }

  return nodes.size();
}

// xml_t::get_value ========================================================

bool xml_t::get_value( std::string&       value,
		       xml_node_t*        root,
		       const std::string& path )
{
  std::string key;
  xml_node_t* node = split_path( root, key, path );
  if( ! node ) return false;

  xml_parm_t* parm = node -> get_parm( key );
  if( ! parm ) return false;
  
  value = parm -> value_str;
  
  return true;
}

// xml_t::get_value ========================================================

bool xml_t::get_value( int&               value,
		       xml_node_t*        root,
		       const std::string& path )
{
  std::string key;
  xml_node_t* node = split_path( root, key, path );
  if( ! node ) return false;

  xml_parm_t* parm = node -> get_parm( key );
  if( ! parm ) return false;
  
  value = atoi( parm -> value_str.c_str() );
  
  return true;
}

// xml_t::get_value ========================================================

bool xml_t::get_value( double&            value,
		       xml_node_t*        root,
		       const std::string& path )
{
  std::string key;
  xml_node_t* node = split_path( root, key, path );
  if( ! node ) return false;

  xml_parm_t* parm = node -> get_parm( key );
  if( ! parm ) return false;
  
  value = atof( parm -> value_str.c_str() );
  
  return true;
}

// xml_t::print ============================================================

void xml_t::print( xml_node_t* root,
		   FILE*       file,
		   int         spacing )
{
  if( ! root ) return;

  fprintf( file, "%*s%s", spacing, "", root -> name() );

  int num_parms = root -> parameters.size();
  for( int i=0; i < num_parms; i++ )
  {
    xml_parm_t& parm = root -> parameters[ i ];
    fprintf( file, " %s=\"%s\"", parm.name(), parm.value_str.c_str() );
  }
  fprintf( file, "\n" );

  int num_children = root -> children.size();
  for( int i=0; i < num_children; i++ )
  {
    print( root -> children[ i ], file, spacing+2 );
  }
}

// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simcraft.h"

namespace { // ANONYMOUS NAMESPACE ==========================================

// trigger_judgement_of_wisdom ==============================================

struct judgement_of_wisdom_callback_t : public action_callback_t
{
  gain_t* gain;
  proc_t* proc;
  rng_t*  rng;

  judgement_of_wisdom_callback_t( player_t* p ) : action_callback_t( p -> sim, p )
  {
    gain = p -> get_gain( "judgement_of_wisdom"      );
    proc = p -> get_proc( "judgement_of_wisdom", sim );
    rng  = p -> get_rng ( "judgement_of_wisdom"      );
  }

  virtual void trigger( action_t* a )
  {
    sim_t* sim = a -> sim;

    if ( ! sim -> target -> debuffs.judgement_of_wisdom -> check() )
      return;

    player_t* p = a -> player;

    double base_mana = p -> resource_base[ RESOURCE_MANA ];

    double PPM = 15.0;
    double proc_chance = 0;

    if ( a -> weapon )
    {
      proc_chance = a -> weapon -> proc_chance_on_swing( PPM, a -> time_to_execute );
    }
    else
    {
      double time_to_execute = a -> channeled ? a -> time_to_tick : a -> time_to_execute;

      if ( time_to_execute == 0 ) time_to_execute = p -> base_gcd;

      proc_chance = PPM * time_to_execute / 60.0;
    }

    if ( ! rng -> roll( proc_chance ) )
      return;

    proc -> occur();

    p -> resource_gain( RESOURCE_MANA, base_mana * 0.02, gain );
  }
};

// init_replenish_targets ================================================

static void init_replenish_targets( sim_t* sim )
{
  if ( sim -> replenishment_candidates.empty() )
  {
    for ( player_t* p = sim -> player_list; p; p = p -> next )
    {
      if ( p -> type != PLAYER_GUARDIAN &&
           p -> primary_resource() == RESOURCE_MANA )
      {
        sim -> replenishment_candidates.push_back( p );
      }
    }
  }
}

// choose_replenish_targets ==============================================

static void choose_replenish_targets( player_t* provider )
{
  sim_t* sim = provider -> sim;

  init_replenish_targets( sim );

  std::vector<player_t*>& candidates = sim      -> replenishment_candidates;
  std::vector<player_t*>& targets    = provider -> replenishment_targets;

  bool replenishment_missing = false;

  for ( int i = candidates.size() - 1; i >= 0; i-- )
  {
    player_t* p = candidates[ i ];

    if ( ! p -> sleeping && ! p -> buffs.replenishment -> check() )
    {
      replenishment_missing = true;
      break;
    }
  }

  // If replenishment is not missing from any of the candidates, then the target list will not change

  if ( ! replenishment_missing ) return;

  for ( int i = targets.size() - 1; i >= 0; i-- )
  {
    targets[ i ] -> buffs.replenishment -> current_value = -1.0;
  }
  targets.clear();

  for ( int i=0; i < sim -> replenishment_targets; i++ )
  {
    player_t* min_player=0;
    double    min_mana=0;

    for ( int j = candidates.size() - 1; j >= 0; j-- )
    {
      player_t* p = candidates[ j ];

      if ( p -> sleeping || ( p -> buffs.replenishment -> current_value == 1.0 ) ) continue;

      if ( ! min_player || min_mana > p -> resource_current[ RESOURCE_MANA ] )
      {
        min_player = p;
        min_mana   = p -> resource_current[ RESOURCE_MANA ];
      }
    }
    if ( min_player )
    {
      min_player -> buffs.replenishment -> trigger( 1, 1.0 );
      targets.push_back( min_player );
    }
    else break;
  }

  for ( int i = candidates.size() - 1; i >= 0; i-- )
  {
    player_t* p = candidates[ i ];

    if ( p -> buffs.replenishment -> current_value < 0 )
    {
      p -> buffs.replenishment -> expire();
    }
  }
}

// replenish_raid ========================================================

static void replenish_raid( player_t* provider )
{
  for ( player_t* p = provider -> sim -> player_list; p; p = p -> next )
  {
    p -> buffs.replenishment -> trigger();
  }
}

// trigger_armor_snapshots ==================================================

static void trigger_armor_snapshots( player_t* p )
{
  assert( p -> sim -> armor_update_interval > 0 );

  struct armor_snapshot_event_t : public event_t
  {
    armor_snapshot_event_t( sim_t* sim, player_t* player, double interval ) : event_t( sim, player )
    {
      name = "Armor Snapshot";
      sim -> add_event( this, interval );
    }
    virtual void execute()
    {
      if ( sim -> debug )
        log_t::output( sim, "Armor Snapshot: %.02f -> %.02f", player -> armor_snapshot, player -> composite_armor() );

      player -> armor_snapshot = player -> composite_armor();

      new ( sim ) armor_snapshot_event_t( sim, player, sim -> armor_update_interval );
    }
  };

  p -> armor_snapshot = p -> composite_armor();

  new ( p -> sim ) armor_snapshot_event_t( p -> sim, p, p -> sim -> current_iteration % p -> sim -> armor_update_interval );
}

// trigger_target_swings ====================================================

static void trigger_target_swings( player_t* p )
{
  assert( p -> sim -> target -> attack_speed > 0 );

  struct target_swing_event_t : public event_t
  {
    target_swing_event_t( sim_t* sim, player_t* player, double interval ) : event_t( sim, player )
    {
      name = "Target Swing";
      sim -> add_event( this, interval );
    }
    virtual void execute()
    {
      if ( sim -> log )
        log_t::output( sim, "%s swings at %s", sim -> target -> name(), player -> name() );

      player -> target_swing();

      player -> target_auto_attack = new ( sim ) target_swing_event_t( sim, player, sim -> target -> attack_speed );
    }
  };

  p -> target_auto_attack = new ( p -> sim ) target_swing_event_t( p -> sim, p, p -> sim -> target -> attack_speed );
}

// parse_talent_url =========================================================

static bool parse_talent_url( sim_t* sim,
                              const std::string& name,
                              const std::string& url )
{
  assert( name == "talents" );

  player_t* p = sim -> active_player;

  p -> talents_str = url;

  std::string::size_type cut_pt;

  if ( url.find( "worldofwarcraft" ) != url.npos )
  {
    if ( ( cut_pt = url.find_first_of( "=" ) ) != url.npos )
    {
      return p -> parse_talents_armory( url.substr( cut_pt + 1 ) );
    }
  }
  else if ( url.find( "wowarmory" ) != url.npos )
  {
    if ( ( cut_pt = url.find_last_of( "=" ) ) != url.npos )
    {
      return p -> parse_talents_armory( url.substr( cut_pt + 1 ) );
    }
  }
  else if ( url.find( "mmo-champion" ) != url.npos )
  {
    std::vector<std::string> parts;
    int part_count = util_t::string_split( parts, url, "&" );
    assert( part_count > 0 );

    if ( ( cut_pt = parts[ 0 ].find_first_of( "=" ) ) != parts[ 0 ].npos )
    {
      return p -> parse_talents_mmo( parts[ 0 ].substr( cut_pt + 1 ) );
    }
  }
  else if ( url.find( "wowhead" ) != url.npos )
  {
    if ( ( cut_pt = url.find_first_of( "#=" ) ) != url.npos )
    {
      return p -> parse_talents_wowhead( url.substr( cut_pt + 1 ) );
    }
  }

  util_t::printf( "simcraft: Unable to decode talent string %s for %s\n", url.c_str(), p -> name() );

  return false;
}

} // ANONYMOUS NAMESPACE ===================================================


// ==========================================================================
// Player
// ==========================================================================

// player_t::player_t =======================================================

player_t::player_t( sim_t*             s,
                    int                t,
                    const std::string& n,
                    int                r ) :
    sim( s ), name_str( n ),
    region_str( s->default_region_str ), server_str( s->default_server_str ), origin_str( "unknown" ),
    next( 0 ), index( -1 ), type( t ), level( 80 ), tank( 0 ),
    party( 0 ), member( 0 ),
    skill( s->default_skill ), distance( 0 ), gcd_ready( 0 ), base_gcd( 1.5 ),
    potion_used( 0 ), sleeping( 0 ), initialized( 0 ),
    pet_list( 0 ), last_modified( 0 ), race_str( "" ), race( r ),
    // Haste
    base_haste_rating( 0 ), initial_haste_rating( 0 ), haste_rating( 0 ), spell_haste( 1.0 ), attack_haste( 1.0 ),
    // Spell Mechanics
    base_spell_power( 0 ),
    base_spell_hit( 0 ),         initial_spell_hit( 0 ),         spell_hit( 0 ),
    base_spell_crit( 0 ),        initial_spell_crit( 0 ),        spell_crit( 0 ),
    base_spell_penetration( 0 ), initial_spell_penetration( 0 ), spell_penetration( 0 ),
    base_mp5( 0 ),               initial_mp5( 0 ),               mp5( 0 ),
    spell_power_multiplier( 1.0 ),  initial_spell_power_multiplier( 1.0 ),
    spell_power_per_intellect( 0 ), initial_spell_power_per_intellect( 0 ),
    spell_power_per_spirit( 0 ),    initial_spell_power_per_spirit( 0 ),
    spell_crit_per_intellect( 0 ),  initial_spell_crit_per_intellect( 0 ),
    mp5_per_intellect( 0 ),
    mana_regen_base( 0 ), mana_regen_while_casting( 0 ),
    energy_regen_per_second( 0 ), focus_regen_per_second( 0 ),
    last_cast( 0 ),
    // Attack Mechanics
    base_attack_power( 0 ),       initial_attack_power( 0 ),        attack_power( 0 ),
    base_attack_hit( 0 ),         initial_attack_hit( 0 ),          attack_hit( 0 ),
    base_attack_expertise( 0 ),   initial_attack_expertise( 0 ),    attack_expertise( 0 ),
    base_attack_crit( 0 ),        initial_attack_crit( 0 ),         attack_crit( 0 ),
    base_attack_penetration( 0 ), initial_attack_penetration( 0 ),  attack_penetration( 0 ),
    attack_power_multiplier( 1.0 ), initial_attack_power_multiplier( 1.0 ),
    attack_power_per_strength( 0 ), initial_attack_power_per_strength( 0 ),
    attack_power_per_agility( 0 ),  initial_attack_power_per_agility( 0 ),
    attack_crit_per_agility( 0 ),   initial_attack_crit_per_agility( 0 ),
    position( POSITION_BACK ),
    // Defense Mechanics
    target_auto_attack( 0 ), base_armor( 0 ), initial_armor( 0 ), armor( 0 ), armor_snapshot( 0 ),
    armor_per_agility( 2.0 ), use_armor_snapshot( false ),
    // Resources
    mana_per_intellect( 0 ), health_per_stamina( 0 ),
    // Consumables
    elixir_guardian( ELIXIR_NONE ),
    elixir_battle( ELIXIR_NONE ),
    flask( FLASK_NONE ),
    food( FOOD_NONE ),
    // Events
    executing( 0 ), channeling( 0 ), readying( 0 ), in_combat( false ), action_queued( false ),
    // Actions
    action_list( 0 ), action_list_default( 0 ),
    // Reporting
    quiet( 0 ), last_foreground_action( 0 ),
    current_time( 0 ), total_seconds( 0 ), total_waiting( 0 ), iteration_dmg( 0 ), total_dmg( 0 ),
    dps( 0 ), dps_min( 0 ), dps_max( 0 ), dps_std_dev( 0 ), dps_error( 0 ), dpr( 0 ), rps_gain( 0 ), rps_loss( 0 ),
    buff_list( 0 ), proc_list( 0 ), gain_list( 0 ), stats_list( 0 ), uptime_list( 0 ),
    save_str( "" ), save_gear_str( "" ), save_talents_str( "" ), save_actions_str( "" ),
    comment_str( "" ),
    meta_gem( META_GEM_NONE ), normalized_to( STAT_NONE ), scaling_lag( 0 ), rng_list( 0 )
{
  if ( sim -> debug ) log_t::output( sim, "Creating Player %s", name() );
  player_t** last = &( sim -> player_list );
  while ( *last ) last = &( ( *last ) -> next );
  *last = this;
  next = 0;
  index = ++( sim -> num_players );

  race_str = util_t::race_type_string( race );

  if ( is_pet() ) skill = 1.0;

  for ( int i=0; i < ATTRIBUTE_MAX; i++ )
  {
    attribute[ i ] = attribute_base[ i ] = attribute_initial[ i ] = 0;
    attribute_multiplier[ i ] = attribute_multiplier_initial[ i ] = 1.0;
  }

  for ( int i=0; i <= SCHOOL_MAX; i++ )
  {
    initial_spell_power[ i ] = spell_power[ i ] = 0;
  }

  for ( int i=0; i < RESOURCE_MAX; i++ )
  {
    resource_base[ i ] = resource_initial[ i ] = resource_max[ i ] = resource_current[ i ] = 0;

    resource_lost[ i ] = resource_gained[ i ] = 0;
  }

  for ( int i=0; i < PROFESSION_MAX; i++ ) profession[ i ] = 0;
  for ( int i=0; i < STAT_MAX; i++ ) scales_with[ i ] = 0;

  items.resize( SLOT_MAX );
  for ( int i=0; i < SLOT_MAX; i++ )
  {
    items[ i ].slot = i;
    items[ i ].sim = sim;
    items[ i ].player = this;
  }

  main_hand_weapon.slot = SLOT_MAIN_HAND;
  off_hand_weapon.slot = SLOT_OFF_HAND;
  ranged_weapon.slot = SLOT_RANGED;

  if ( ! sim -> active_files.empty() ) origin_str = sim -> active_files.back();
}

// player_t::~player_t =====================================================

player_t::~player_t()
{
  while ( action_t* a = action_list )
  {
    action_list = a -> next;
    delete a;
  }
  while ( proc_t* p = proc_list )
  {
    proc_list = p -> next;
    delete p;
  }
  while ( gain_t* g = gain_list )
  {
    gain_list = g -> next;
    delete g;
  }
  while ( stats_t* s = stats_list )
  {
    stats_list = s -> next;
    delete s;
  }
  while ( uptime_t* u = uptime_list )
  {
    uptime_list = u -> next;
    delete u;
  }
  while ( rng_t* r = rng_list )
  {
    rng_list = r -> next;
    delete r;
  }
}

// player_t::id ============================================================

const char* player_t::id()
{
  if ( id_str.empty() )
  {
    // create artifical unit ID, format type+subtype+id= TTTSSSSSSSIIIIII
    char buffer[ 1024 ];
    sprintf( buffer, "0x0100000002%06X,\"%s\",0x511", index, name_str.c_str() );
    id_str = buffer;
  }

  return id_str.c_str();
}

// player_t::init ==========================================================

void player_t::init( sim_t* sim )
{
  if ( sim -> debug ) log_t::output( sim, "Initializing Auras, Buffs, and De-Buffs." );

  player_t::death_knight_init( sim );
  player_t::druid_init       ( sim );
  player_t::hunter_init      ( sim );
  player_t::mage_init        ( sim );
  player_t::paladin_init     ( sim );
  player_t::priest_init      ( sim );
  player_t::rogue_init       ( sim );
  player_t::shaman_init      ( sim );
  player_t::warlock_init     ( sim );
  player_t::warrior_init     ( sim );

  for ( player_t* p = sim -> player_list; p; p = p -> next )
  {
    p -> init_resources( true );
  }  
}

// player_t::init ==========================================================

void player_t::init()
{
  if ( sim -> debug ) log_t::output( sim, "Initializing player %s", name() );

  initialized = 1;

  init_rating();
  init_glyphs();
  init_race();
  init_base();
  init_items();
  init_core();
  init_spell();
  init_attack();
  init_defense();
  init_weapon( &main_hand_weapon );
  init_weapon( &off_hand_weapon );
  init_weapon( &ranged_weapon );
  init_unique_gear();
  init_enchant();
  init_professions();
  init_scaling();
  init_consumables();
  init_buffs();
  init_actions();
  init_gains();
  init_procs();
  init_uptimes();
  init_rng();
  init_stats();
  init_expressions();
}

// player_t::init_items =====================================================

void player_t::init_items()
{
  if ( is_pet() ) return;

  std::vector<std::string> splits;
  int num_splits = util_t::string_split( splits, items_str, "/" );
  for ( int i=0; i < num_splits; i++ )
  {
    if ( find_item( splits[ i ] ) )
    {
      util_t::printf( "simcraft: Player %s has multiple %s equipped.\n", name(), splits[ i ].c_str() );
    }
    items.push_back( item_t( this, splits[ i ] ) );
  }

  gear_stats_t item_stats;

  int num_items = items.size();
  for ( int i=0; i < num_items; i++ )
  {
    item_t& item = items[ i ];

    if ( ! item.init() )
    {
      util_t::printf( "simcraft: Unable to initialize item '%s' on player '%s'\n", item.name(), name() );
      assert( false );
    }

    for ( int j=0; j < STAT_MAX; j++ )
    {
      item_stats.add_stat( j, item.stats.get_stat( j ) );
    }
  }

  init_meta_gem( item_stats );

  for ( int i=0; i < STAT_MAX; i++ )
  {
    if ( gear.get_stat( i ) == 0 )
    {
      gear.set_stat( i, item_stats.get_stat( i ) );
    }
  }

  if ( sim -> debug )
  {
    log_t::output( sim, "%s gear:", name() );
    gear.print( sim -> output_file );
  }

  set_bonus.init( this );
}

// player_t::init_meta_gem ==================================================

void player_t::init_meta_gem( gear_stats_t& item_stats )
{
  if ( ! meta_gem_str.empty() ) meta_gem = util_t::parse_meta_gem_type( meta_gem_str );

  if      ( meta_gem == META_AUSTERE_EARTHSIEGE      ) item_stats.attribute[ ATTR_STAMINA ] += 32;
  else if ( meta_gem == META_BEAMING_EARTHSIEGE      ) item_stats.crit_rating += 21;
  else if ( meta_gem == META_BRACING_EARTHSIEGE      ) item_stats.spell_power += 25;
  else if ( meta_gem == META_BRACING_EARTHSTORM      ) item_stats.spell_power += 14;
  else if ( meta_gem == META_CHAOTIC_SKYFIRE         ) item_stats.crit_rating += 12;
  else if ( meta_gem == META_CHAOTIC_SKYFLARE        ) item_stats.crit_rating += 21;
  else if ( meta_gem == META_EMBER_SKYFLARE          ) item_stats.spell_power += 25;
  else if ( meta_gem == META_ENIGMATIC_SKYFLARE      ) item_stats.crit_rating += 21;
  else if ( meta_gem == META_ENIGMATIC_STARFLARE     ) item_stats.crit_rating += 17;
  else if ( meta_gem == META_ENIGMATIC_SKYFIRE       ) item_stats.crit_rating += 12;
  else if ( meta_gem == META_FORLORN_SKYFLARE        ) item_stats.spell_power += 25;
  else if ( meta_gem == META_FORLORN_STARFLARE       ) item_stats.spell_power += 20;
  else if ( meta_gem == META_IMPASSIVE_SKYFLARE      ) item_stats.crit_rating += 21;
  else if ( meta_gem == META_IMPASSIVE_STARFLARE     ) item_stats.crit_rating += 17;
  else if ( meta_gem == META_INSIGHTFUL_EARTHSIEGE   ) item_stats.attribute[ ATTR_INTELLECT ] += 21;
  else if ( meta_gem == META_INSIGHTFUL_EARTHSTORM   ) item_stats.attribute[ ATTR_INTELLECT ] += 12;
  else if ( meta_gem == META_PERSISTENT_EARTHSHATTER ) item_stats.attack_power += 34;
  else if ( meta_gem == META_PERSISTENT_EARTHSIEGE   ) item_stats.attack_power += 42;
  else if ( meta_gem == META_POWERFUL_EARTHSHATTER   ) item_stats.attribute[ ATTR_STAMINA ] += 26;
  else if ( meta_gem == META_POWERFUL_EARTHSIEGE     ) item_stats.attribute[ ATTR_STAMINA ] += 32;
  else if ( meta_gem == META_POWERFUL_EARTHSTORM     ) item_stats.attribute[ ATTR_STAMINA ] += 18;
  else if ( meta_gem == META_RELENTLESS_EARTHSIEGE   ) item_stats.attribute[ ATTR_AGILITY ] += 21;
  else if ( meta_gem == META_RELENTLESS_EARTHSTORM   ) item_stats.attribute[ ATTR_AGILITY ] += 12;
  else if ( meta_gem == META_REVITALIZING_SKYFLARE   ) item_stats.mp5 += 8;
  else if ( meta_gem == META_SWIFT_SKYFIRE           ) item_stats.attack_power += 24;
  else if ( meta_gem == META_SWIFT_SKYFLARE          ) item_stats.attack_power += 42;
  else if ( meta_gem == META_SWIFT_STARFIRE          ) item_stats.spell_power  += 12;
  else if ( meta_gem == META_SWIFT_STARFLARE         ) item_stats.attack_power += 34;
  else if ( meta_gem == META_TIRELESS_STARFLARE      ) item_stats.spell_power  += 20;

  if ( meta_gem == META_EMBER_SKYFLARE )
  {
    attribute_multiplier_initial[ ATTR_INTELLECT ] *= 1.02;
  }
  else if ( meta_gem == META_BEAMING_EARTHSIEGE )
  {
    mana_per_intellect *= 1.02;
  }
  else if ( meta_gem == META_MYSTICAL_SKYFIRE )
  {
    unique_gear_t::register_stat_proc( PROC_SPELL, RESULT_HIT_MASK, "mystical_skyfire", this, STAT_HASTE_RATING, 1, 320, 0.15, 4.0, 45.0 );
  }
  else if ( meta_gem == META_INSIGHTFUL_EARTHSTORM )
  {
    unique_gear_t::register_stat_proc( PROC_SPELL, RESULT_HIT_MASK, "insightful_earthstorm", this, STAT_MANA, 1, 300, 0.05, 0, 15.0 );
  }
  else if ( meta_gem == META_INSIGHTFUL_EARTHSIEGE )
  {
    unique_gear_t::register_stat_proc( PROC_SPELL, RESULT_HIT_MASK, "insightful_earthsiege", this, STAT_MANA, 1, 600, 0.05, 0, 15.0 );
  }
}

// player_t::init_core ======================================================

void player_t::init_core()
{
  stats.  hit_rating = gear.  hit_rating + enchant.  hit_rating + sim -> enchant.  hit_rating;
  stats. crit_rating = gear. crit_rating + enchant. crit_rating + sim -> enchant. crit_rating;
  stats.haste_rating = gear.haste_rating + enchant.haste_rating + sim -> enchant.haste_rating;

  initial_haste_rating = stats.haste_rating;

  for ( int i=0; i < ATTRIBUTE_MAX; i++ )
  {
    stats.attribute[ i ] = gear.attribute[ i ] + enchant.attribute[ i ] + sim -> enchant.attribute[ i ];

    attribute[ i ] = attribute_initial[ i ] = attribute_base[ i ] + stats.attribute[ i ];
  }
}

// player_t::init_race ======================================================

void player_t::init_race()
{
  race = util_t::parse_race_type( race_str );
}

// player_t::init_spell =====================================================

void player_t::init_spell()
{
  stats.spell_power       = gear.spell_power       + enchant.spell_power       + sim -> enchant.spell_power;
  stats.spell_penetration = gear.spell_penetration + enchant.spell_penetration + sim -> enchant.spell_penetration;
  stats.mp5               = gear.mp5               + enchant.mp5               + sim -> enchant.mp5;

  initial_spell_power[ SCHOOL_MAX ] = base_spell_power + stats.spell_power;

  initial_spell_hit = base_spell_hit + stats.hit_rating / rating.spell_hit;

  initial_spell_crit = base_spell_crit + stats.crit_rating / rating.spell_crit;

  initial_spell_penetration = base_spell_penetration + stats.spell_penetration;

  initial_mp5 = base_mp5 + stats.mp5;

  double base_60 = 0.006600;
  double base_70 = 0.005596;
  double base_80 = 0.003345;

  mana_regen_base = rating_t::interpolate( level, base_60, base_70, base_80 );
}

// player_t::init_attack ====================================================

void player_t::init_attack()
{
  stats.attack_power             = gear.attack_power             + enchant.attack_power             + sim -> enchant.attack_power;
  stats.expertise_rating         = gear.expertise_rating         + enchant.expertise_rating         + sim -> enchant.expertise_rating;
  stats.armor_penetration_rating = gear.armor_penetration_rating + enchant.armor_penetration_rating + sim -> enchant.armor_penetration_rating;

  initial_attack_power = base_attack_power + stats.attack_power;

  initial_attack_hit = base_attack_hit + stats.hit_rating / rating.attack_hit;

  initial_attack_crit = base_attack_crit + stats.crit_rating / rating.attack_crit;

  initial_attack_expertise = base_attack_expertise + stats.expertise_rating / rating.expertise;

  initial_attack_penetration = base_attack_penetration + stats.armor_penetration_rating / rating.armor_penetration;
}

// player_t::init_defense ====================================================

void player_t::init_defense()
{
  stats.armor = gear.armor + enchant.armor + sim -> enchant.armor;

  initial_armor = base_armor + stats.armor;
}

// player_t::init_weapon ===================================================

void player_t::init_weapon( weapon_t* w )
{
  if ( w -> type == WEAPON_NONE ) return;

  if ( w -> slot == SLOT_MAIN_HAND ) assert( w -> type >= WEAPON_NONE && w -> type < WEAPON_2H );
  if ( w -> slot == SLOT_OFF_HAND  ) assert( w -> type >= WEAPON_NONE && w -> type < WEAPON_2H );
  if ( w -> slot == SLOT_RANGED    ) assert( w -> type == WEAPON_NONE || ( w -> type > WEAPON_2H && w -> type < WEAPON_RANGED ) );
}

// player_t::init_unique_gear ==============================================

void player_t::init_unique_gear()
{
  unique_gear_t::init( this );
}

// player_t::init_enchant ==================================================

void player_t::init_enchant()
{
  enchant_t::init( this );
}

// player_t::init_resources ================================================

void player_t::init_resources( bool force )
{
  // The first 20pts of intellect/stamina only provide 1pt of mana/health.
  // Code simplified on the assumption that the minimum player level is 60.
  double adjust = is_pet() ? 0 : 20;

  for ( int i=0; i < RESOURCE_MAX; i++ )
  {
    if ( force || resource_initial[ i ] == 0 )
    {
      resource_initial[ i ] = resource_base[ i ] + gear.resource[ i ] + enchant.resource[ i ] + sim -> enchant.resource[ i ];

      if ( i == RESOURCE_MANA   ) resource_initial[ i ] += ( intellect() - adjust ) * mana_per_intellect + adjust;
      if ( i == RESOURCE_HEALTH ) resource_initial[ i ] += (   stamina() - adjust ) * health_per_stamina + adjust;
    }
    resource_current[ i ] = resource_max[ i ] = resource_initial[ i ];
  }

  if ( timeline_resource.empty() )
  {
    int size = ( int ) sim -> max_time;
    if ( size == 0 ) size = 600; // Default to 10 minutes
    size *= 2;
    timeline_resource.insert( timeline_resource.begin(), size, 0 );
  }
}

// player_t::init_consumables ==============================================

void player_t::init_consumables()
{
  consumable_t::init_flask  ( this );
  consumable_t::init_elixirs( this );
  consumable_t::init_food   ( this );
}

// player_t::init_professions ==============================================

void player_t::init_professions()
{
  if ( professions_str.empty() ) return;

  std::vector<std::string> splits;
  int size = util_t::string_split( splits, professions_str, ",/" );

  for ( int i=0; i < size; i++ )
  {
    std::string prof_name;
    int prof_value=0;

    if ( 2 != util_t::string_split( splits[ i ], "=", "S i", &prof_name, &prof_value ) )
    {
      prof_name  = splits[ i ];
      prof_value = 450;
    }

    int prof_type = util_t::parse_profession_type( prof_name );
    if ( prof_type == PROFESSION_NONE )
    {
      util_t::printf( "Invalid profession encoding: %s\n", professions_str.c_str() );
      assert( 0 );
    }

    profession[ prof_type ] = prof_value;
  }

  // Miners gain additional stamina
  if      ( profession[ PROF_MINING ] >= 450 ) attribute_initial[ ATTR_STAMINA ] += 50.0;
  else if ( profession[ PROF_MINING ] >= 375 ) attribute_initial[ ATTR_STAMINA ] += 30.0;
  else if ( profession[ PROF_MINING ] >= 300 ) attribute_initial[ ATTR_STAMINA ] += 10.0;
  else if ( profession[ PROF_MINING ] >= 225 ) attribute_initial[ ATTR_STAMINA ] +=  7.0;
  else if ( profession[ PROF_MINING ] >= 150 ) attribute_initial[ ATTR_STAMINA ] +=  5.0;
  else if ( profession[ PROF_MINING ] >=  75 ) attribute_initial[ ATTR_STAMINA ] +=  3.0;

  // Skinners gain additional crit rating
  if      ( profession[ PROF_SKINNING ] >= 450 ) initial_attack_crit += 32.0 / rating.attack_crit;
  else if ( profession[ PROF_SKINNING ] >= 375 ) initial_attack_crit += 20.0 / rating.attack_crit;
  else if ( profession[ PROF_SKINNING ] >= 300 ) initial_attack_crit += 12.0 / rating.attack_crit;
  else if ( profession[ PROF_SKINNING ] >= 225 ) initial_attack_crit +=  9.0 / rating.attack_crit;
  else if ( profession[ PROF_SKINNING ] >= 150 ) initial_attack_crit +=  6.0 / rating.attack_crit;
  else if ( profession[ PROF_SKINNING ] >=  75 ) initial_attack_crit +=  3.0 / rating.attack_crit;
}

// player_t::init_actions ==================================================

void player_t::init_actions()
{
  if ( ! action_list_str.empty() )
  {
    if ( action_list_default && sim -> debug ) log_t::output( sim, "Player %s using default actions", name() );

    if ( sim -> debug ) log_t::output( sim, "Player %s: action_list_str=%s", name(), action_list_str.c_str() );

    std::vector<std::string> splits;
    int num_splits = util_t::string_split( splits, action_list_str, "/" );

    for ( int i=0; i < num_splits; i++ )
    {
      std::string action_name    = splits[ i ];
      std::string action_options = "";

      std::string::size_type cut_pt = action_name.find_first_of( ",:" );

      if ( cut_pt != action_name.npos )
      {
        action_options = action_name.substr( cut_pt + 1 );
        action_name    = action_name.substr( 0, cut_pt );
      }

      if ( ! create_action( action_name, action_options ) )
      {
        util_t::printf( "player_t: Unknown action: %s\n", splits[ i ].c_str() );
        assert( false );
      }
    }
  }

  if ( ! action_list_skip.empty() )
  {
    if ( sim -> debug ) log_t::output( sim, "Player %s: action_list_skip=%s", name(), action_list_skip.c_str() );
    std::vector<std::string> splits;
    int num_splits = util_t::string_split( splits, action_list_skip, "/" );
    for ( int i=0; i < num_splits; i++ )
    {
      action_t* action = find_action( splits[ i ] );
      if ( action ) action -> background = true;
    }
  }
}

// player_t::init_rating ===================================================

void player_t::init_rating()
{
  rating.init( sim, level );
}

// player_t::init_buffs ====================================================

void player_t::init_buffs()
{
  buffs.berserking      = new buff_t( this, "berserking",      1, 10.0 );
  buffs.heroic_presence = new buff_t( this, "heroic_presence", 1       );
  buffs.replenishment   = new buff_t( this, "replenishment",   1, 15.0 );
  buffs.stoneform       = new buff_t( this, "stoneform",       1,  8.0 );

  // Infinite-Stacking Buffs
  buffs.moving  = new buff_t( this, "moving",  -1 );
  buffs.stunned = new buff_t( this, "stunned", -1 );

  // stat_buff_t( sim, player, name, stat, amount, max_stack, duration, cooldown, proc_chance, quiet )
  buffs.blood_fury_ap   = new stat_buff_t( this, "blood_fury_ap", STAT_ATTACK_POWER, ( level * 4 ) + 2, 1, 15.0 );
  buffs.blood_fury_sp   = new stat_buff_t( this, "blood_fury_sp", STAT_SPELL_POWER,  ( level * 2 ) + 3, 1, 15.0 );
}

// player_t::init_gains ====================================================

void player_t::init_gains()
{
  gains.arcane_torrent         = get_gain( "arcane_torrent" );
  gains.blessing_of_wisdom     = get_gain( "blessing_of_wisdom" );
  gains.dark_rune              = get_gain( "dark_rune" );
  gains.energy_regen           = get_gain( "energy_regen" );
  gains.focus_regen            = get_gain( "focus_regen" );
  gains.innervate              = get_gain( "innervate" );
  gains.judgement_of_wisdom    = get_gain( "judgement_of_wisdom" );
  gains.mana_potion            = get_gain( "mana_potion" );
  gains.mana_spring_totem      = get_gain( "mana_spring_totem" );
  gains.mana_tide              = get_gain( "mana_tide" );
  gains.mp5_regen              = get_gain( "mp5_regen" );
  gains.replenishment          = get_gain( "replenishment" );
  gains.restore_mana           = get_gain( "restore_mana" );
  gains.spellsurge             = get_gain( "spellsurge" );
  gains.spirit_intellect_regen = get_gain( "spirit_intellect_regen" );
  gains.vampiric_embrace       = get_gain( "vampiric_embrace" );
  gains.vampiric_touch         = get_gain( "vampiric_touch" );
  gains.water_elemental        = get_gain( "water_elemental" );
}

// player_t::init_procs ====================================================

void player_t::init_procs()
{
  procs.hat_donor = get_proc( "hat_donor", sim );
}

// player_t::init_uptimes ==================================================

void player_t::init_uptimes()
{
}

// player_t::init_rng ======================================================

void player_t::init_rng()
{
  rngs.lag_channel = get_rng( "lag_channel" );
  rngs.lag_gcd     = get_rng( "lag_gcd"     );
  rngs.lag_queue   = get_rng( "lag_queue"   );
}

// player_t::init_stats ====================================================

void player_t::init_stats()
{
  for ( int i=0; i < RESOURCE_MAX; i++ )
  {
    resource_lost[ i ] = resource_gained[ i ] = 0;
  }

  iteration_dps.clear();
  iteration_dps.insert( iteration_dps.begin(), sim -> iterations, 0 );
}

// player_t::init_scaling ==================================================

void player_t::init_scaling()
{
  if ( ! is_pet() )
  {
    int role = primary_role();

    int attack = ( ( role == ROLE_ATTACK ) || ( role == ROLE_HYBRID ) ) ? 1 : 0;
    int spell  = ( ( role == ROLE_SPELL  ) || ( role == ROLE_HYBRID ) ) ? 1 : 0;

    scales_with[ STAT_STRENGTH  ] = attack;
    scales_with[ STAT_AGILITY   ] = attack;
    scales_with[ STAT_STAMINA   ] = 0;
    scales_with[ STAT_INTELLECT ] = spell;
    scales_with[ STAT_SPIRIT    ] = spell;

    scales_with[ STAT_HEALTH ] = 0;
    scales_with[ STAT_MANA   ] = 0;
    scales_with[ STAT_RAGE   ] = 0;
    scales_with[ STAT_ENERGY ] = 0;
    scales_with[ STAT_FOCUS  ] = 0;
    scales_with[ STAT_RUNIC  ] = 0;

    scales_with[ STAT_SPELL_POWER       ] = spell;
    scales_with[ STAT_SPELL_PENETRATION ] = 0;
    scales_with[ STAT_MP5               ] = 0;

    scales_with[ STAT_ATTACK_POWER             ] = attack;
    scales_with[ STAT_EXPERTISE_RATING         ] = attack;
    scales_with[ STAT_ARMOR_PENETRATION_RATING ] = attack;

    scales_with[ STAT_HIT_RATING   ] = 1;
    scales_with[ STAT_CRIT_RATING  ] = 1;
    scales_with[ STAT_HASTE_RATING ] = 1;

    scales_with[ STAT_WEAPON_DPS   ] = attack;
    scales_with[ STAT_WEAPON_SPEED ] = 0;

    scales_with[ STAT_ARMOR ] = 0;

    if ( sim -> scaling -> scale_stat != STAT_NONE )
    {
      double v = sim -> scaling -> scale_value;

      switch ( sim -> scaling -> scale_stat )
      {
      case STAT_STRENGTH:  attribute_initial[ ATTR_STRENGTH  ] += v; break;
      case STAT_AGILITY:   attribute_initial[ ATTR_AGILITY   ] += v; break;
      case STAT_STAMINA:   attribute_initial[ ATTR_STAMINA   ] += v; break;
      case STAT_INTELLECT: attribute_initial[ ATTR_INTELLECT ] += v; break;
      case STAT_SPIRIT:    attribute_initial[ ATTR_SPIRIT    ] += v; break;

      case STAT_SPELL_POWER:       initial_spell_power[ SCHOOL_MAX ] += v; break;
      case STAT_SPELL_PENETRATION: initial_spell_penetration         += v; break;
      case STAT_MP5:               initial_mp5                       += v; break;

      case STAT_ATTACK_POWER:             initial_attack_power       += v;                            break;
      case STAT_EXPERTISE_RATING:         initial_attack_expertise   += v / rating.expertise;         break;
      case STAT_ARMOR_PENETRATION_RATING: initial_attack_penetration += v / rating.armor_penetration; break;

      case STAT_HIT_RATING:
        initial_attack_hit += v / rating.attack_hit;
        initial_spell_hit  += v / rating.spell_hit;
        break;

      case STAT_CRIT_RATING:
        initial_attack_crit += v / rating.attack_crit;
        initial_spell_crit  += v / rating.spell_crit;
        break;

      case STAT_HASTE_RATING: initial_haste_rating += v; break;

      case STAT_WEAPON_DPS:
        main_hand_weapon.damage += main_hand_weapon.swing_time * v;
        ranged_weapon.damage    += ranged_weapon.swing_time * v;
        break;

      case STAT_ARMOR: initial_armor += v; break;

      case STAT_MAX: break;

      default: assert( 0 );
      }
    }
  }
}

// player_t::init_stats ====================================================
// must be LAST init_
void player_t::init_expressions()
{
  // parse all action expressions
  for ( action_t* a = action_list; a; a = a -> next  )
    if ( a->has_if_exp<0 )
    {
      if ( a->if_expression!="" )
        a->if_exp=act_expression_t::create( a, a->if_expression,"", ETP_BOOL );
      a->has_if_exp= ( a->if_exp!=0 );
      if ( a->has_if_exp && sim->debug_exp ) //debug
        util_t::printf( "%s", a->if_exp->to_string().c_str() );
    }
}


// player_t::find_item ======================================================

item_t* player_t::find_item( const std::string& str )
{
  int num_items = items.size();
  for ( int i=0; i < num_items; i++ )
    if ( str == items[ i ].name() )
      return &( items[ i ] );

  return 0;
}

// player_t::composite_attack_power ========================================

double player_t::composite_attack_power() SC_CONST
{
  double ap = attack_power;

  ap += floor( attack_power_per_strength * strength() );
  ap += floor( attack_power_per_agility  * agility() );

  ap += std::max( buffs.blessing_of_might -> value(), sim -> auras.battle_shout -> value() );

  return ap;
}

// player_t::composite_attack_crit =========================================

double player_t::composite_attack_crit() SC_CONST
{
  double ac = attack_crit + attack_crit_per_agility * agility();

  if ( type != PLAYER_GUARDIAN )
  {
    if ( sim -> auras.leader_of_the_pack -> check() || sim -> auras.rampage -> up() )
    {
      ac += 0.05;
    }
  }

  return ac;
}

// player_t::composite_attack_hit ==========================================

double player_t::composite_attack_hit() SC_CONST
{
  double ah = attack_hit;

  if ( buffs.heroic_presence -> check() ) ah += 0.01;

  return ah;
}

// player_t::composite_armor =========================================

double player_t::composite_armor() SC_CONST
{
  double a = armor;

  if ( meta_gem == META_AUSTERE_EARTHSIEGE ) a *= 1.02;

  a += floor( armor_per_agility * agility() );

  if ( buffs.stoneform -> check() ) a *= 1.10;

  return a;
}

// player_t::composite_spell_power ========================================

double player_t::composite_spell_power( int school ) SC_CONST
{
  double sp = spell_power[ school ];

  if ( school == SCHOOL_FROSTFIRE )
  {
    sp = std::max( spell_power[ SCHOOL_FROST ],
                   spell_power[ SCHOOL_FIRE  ] );
  }

  if ( school != SCHOOL_MAX ) sp += spell_power[ SCHOOL_MAX ];

  sp += spell_power_per_intellect * intellect();
  sp += spell_power_per_spirit    * spirit();

  if ( type != PLAYER_GUARDIAN )
  {
    double best_buff = 0;
    if ( sim -> auras.totem_of_wrath -> up() )
    {
      if ( best_buff < sim -> auras.totem_of_wrath -> current_value ) best_buff = sim -> auras.totem_of_wrath -> current_value;
    }
    if ( sim -> auras.flametongue_totem -> up() )
    {
      if ( best_buff < sim -> auras.flametongue_totem -> current_value ) best_buff = sim -> auras.flametongue_totem -> current_value;
    }
    if ( buffs.demonic_pact -> up() )
    {
      if ( best_buff < buffs.demonic_pact -> current_value ) best_buff = buffs.demonic_pact -> current_value;
    }
    sp += best_buff;
  }

  return floor( sp );
}

// player_t::composite_spell_crit ==========================================

double player_t::composite_spell_crit() SC_CONST
{
  double sc = spell_crit + spell_crit_per_intellect * intellect();

  if ( type != PLAYER_GUARDIAN )
  {
    if ( buffs.focus_magic -> check() ) sc += 0.03;

    if ( sim -> auras.elemental_oath -> up() || sim -> auras.moonkin -> check() )
    {
      sc += 0.05;
    }
  }

  return sc;
}

// player_t::composite_spell_hit ===========================================

double player_t::composite_spell_hit() SC_CONST
{
  double sh = spell_hit;

  if ( buffs.heroic_presence -> check() ) sh += 0.01;

  return sh;
}

// player_t::composite_attack_power_multiplier =============================

double player_t::composite_attack_power_multiplier() SC_CONST
{
  double m = attack_power_multiplier;

  if ( sim -> auras.abominations_might -> up() || sim -> auras.trueshot -> up() )
  {
    m *= 1.10;
  }
  else
  {
    m *= 1.0 + sim -> auras.unleashed_rage -> value() * 0.01;
  }

  return m;
}

// player_t::composite_attribute_multiplier ================================

double player_t::composite_attribute_multiplier( int attr ) SC_CONST
{
  double m = attribute_multiplier[ attr ];
  if ( buffs.blessing_of_kings -> check() ) m *= 1.10;
  return m;
}

// player_t::strength() ====================================================

double player_t::strength() SC_CONST
{
  double a = attribute[ ATTR_STRENGTH ];
  a += sim -> auras.strength_of_earth -> value();
  a *= composite_attribute_multiplier( ATTR_STRENGTH );
  return floor( a );
}

// player_t::agility() =====================================================

double player_t::agility() SC_CONST
{
  double a = attribute[ ATTR_AGILITY ];
  a += sim -> auras.strength_of_earth -> value();
  a *= composite_attribute_multiplier( ATTR_AGILITY );
  return floor( a );
}

// player_t::stamina() =====================================================

double player_t::stamina() SC_CONST
{
  double a = attribute[ ATTR_STAMINA ];
  a *= composite_attribute_multiplier( ATTR_STAMINA );
  return floor( a );
}

// player_t::intellect() ===================================================

double player_t::intellect() SC_CONST
{
  double a = attribute[ ATTR_INTELLECT ];
  if ( race == RACE_GNOME )
  {
    a += floor( ( a - attribute_base[ ATTR_INTELLECT ] ) * 0.05 );
  }
  a *= composite_attribute_multiplier( ATTR_INTELLECT );
  return floor( a );
}

// player_t::spirit() ======================================================

double player_t::spirit() SC_CONST
{
  double a = attribute[ ATTR_SPIRIT ];
  if ( race == RACE_HUMAN )
  {
    a += floor( ( a - attribute_base[ ATTR_SPIRIT ] ) * 0.03 );
  }
  a *= composite_attribute_multiplier( ATTR_SPIRIT );
  return floor( a );
}

// player_t::combat_begin ==================================================

void player_t::combat_begin( sim_t* sim )
{
  player_t::death_knight_combat_begin( sim );
  player_t::druid_combat_begin       ( sim );
  player_t::hunter_combat_begin      ( sim );
  player_t::mage_combat_begin        ( sim );
  player_t::paladin_combat_begin     ( sim );
  player_t::priest_combat_begin      ( sim );
  player_t::rogue_combat_begin       ( sim );
  player_t::shaman_combat_begin      ( sim );
  player_t::warlock_combat_begin     ( sim );
  player_t::warrior_combat_begin     ( sim );
}

// player_t::combat_begin ==================================================

void player_t::combat_begin()
{
  if ( sim -> debug ) log_t::output( sim, "Combat begins for player %s", name() );

  if ( action_list && ! is_pet() && ! sleeping )
  {
    schedule_ready();
  }

  if ( sim -> overrides.heroic_presence )
  {
    buffs.heroic_presence -> trigger();
  }
  else if ( race == RACE_DRAENEI )
  {
    buffs.heroic_presence -> trigger();

    if ( party != 0 )
    {
      for( player_t* p = sim -> player_list; p; p = p -> next )
      {
	if( p -> party == party &&
	    p -> type != PLAYER_GUARDIAN )
	{
	  p -> buffs.heroic_presence -> trigger();
	}
      }
    }
  }

  if ( sim -> overrides.replenishment ) buffs.replenishment -> override();

  init_resources( true );

  if ( primary_resource() == RESOURCE_MANA )
  {
    get_gain( "initial_mana" ) -> add( resource_max[ RESOURCE_MANA ] );
  }

  if ( use_armor_snapshot ) trigger_armor_snapshots( this );

  if ( tank ) trigger_target_swings( this );
}

// player_t::combat_end ====================================================

void player_t::combat_end( sim_t* sim )
{
  player_t::death_knight_combat_end( sim );
  player_t::druid_combat_end       ( sim );
  player_t::hunter_combat_end      ( sim );
  player_t::mage_combat_end        ( sim );
  player_t::paladin_combat_end     ( sim );
  player_t::priest_combat_end      ( sim );
  player_t::rogue_combat_end       ( sim );
  player_t::shaman_combat_end      ( sim );
  player_t::warlock_combat_end     ( sim );
  player_t::warrior_combat_end     ( sim );
}

// player_t::combat_end ====================================================

void player_t::combat_end()
{
  if ( sim -> debug ) log_t::output( sim, "Combat ends for player %s", name() );

  for( buff_t* b = buff_list; b; b = b -> next )
  {
    b -> expire();
  }

  double iteration_seconds = current_time;

  if ( iteration_seconds > 0 )
  {
    total_seconds += iteration_seconds;

    for ( pet_t* pet = pet_list; pet; pet = pet -> next_pet )
    {
      iteration_dmg += pet -> iteration_dmg;
    }
    iteration_dps[ sim -> current_iteration ] = iteration_dmg / iteration_seconds;
  }
}

// player_t::reset =========================================================

void player_t::reset()
{
  if ( sim -> debug ) log_t::output( sim, "Reseting player %s", name() );

  last_cast = 0;
  gcd_ready = 0;

  haste_rating = initial_haste_rating;
  recalculate_haste();

  for ( int i=0; i < ATTRIBUTE_MAX; i++ )
  {
    attribute           [ i ] = attribute_initial           [ i ];
    attribute_multiplier[ i ] = attribute_multiplier_initial[ i ];
  }

  for ( int i=0; i <= SCHOOL_MAX; i++ )
  {
    spell_power[ i ] = initial_spell_power[ i ];
  }

  spell_hit         = initial_spell_hit;
  spell_crit        = initial_spell_crit;
  spell_penetration = initial_spell_penetration;
  mp5               = initial_mp5;

  attack_power       = initial_attack_power;
  attack_hit         = initial_attack_hit;
  attack_expertise   = initial_attack_expertise;
  attack_crit        = initial_attack_crit;
  attack_penetration = initial_attack_penetration;

  armor              = initial_armor;
  armor_snapshot     = initial_armor;

  spell_power_multiplier    = initial_spell_power_multiplier;
  spell_power_per_intellect = initial_spell_power_per_intellect;
  spell_power_per_spirit    = initial_spell_power_per_spirit;
  spell_crit_per_intellect  = initial_spell_crit_per_intellect;

  attack_power_multiplier   = initial_attack_power_multiplier;
  attack_power_per_strength = initial_attack_power_per_strength;
  attack_power_per_agility  = initial_attack_power_per_agility;
  attack_crit_per_agility   = initial_attack_crit_per_agility;

  for ( buff_t* b = buff_list; b; b = b -> next )
  {
    b -> reset();
  }

  last_foreground_action = 0;
  current_time = 0;

  executing = 0;
  channeling = 0;
  readying = 0;
  in_combat = false;
  iteration_dmg = 0;

  main_hand_weapon.buff_type  = 0;
  main_hand_weapon.buff_value = 0;

  off_hand_weapon.buff_type  = 0;
  off_hand_weapon.buff_value = 0;

  ranged_weapon.buff_type  = 0;
  ranged_weapon.buff_value = 0;

  elixir_battle   = ELIXIR_NONE;
  elixir_guardian = ELIXIR_NONE;
  flask           = FLASK_NONE;
  food            = FOOD_NONE;

  for ( int i=0; i < RESOURCE_MAX; i++ )
  {
    action_callback_t::reset( resource_gain_callbacks[ i ] );
    action_callback_t::reset( resource_loss_callbacks[ i ] );
  }
  for ( int i=0; i < RESULT_MAX; i++ )
  {
    action_callback_t::reset( attack_result_callbacks       [ i ] );
    action_callback_t::reset( spell_result_callbacks        [ i ] );
    action_callback_t::reset( attack_direct_result_callbacks[ i ] );
    action_callback_t::reset( spell_direct_result_callbacks [ i ] );
  }
  action_callback_t::reset( tick_callbacks );
  action_callback_t::reset( tick_damage_callbacks );
  action_callback_t::reset( direct_damage_callbacks );

  replenishment_targets.clear();

  init_resources( true );

  for ( action_t* a = action_list; a; a = a -> next )
  {
    a -> reset();
  }

  potion_used = 0;

  if ( sleeping ) quiet = 1;
}

// player_t::schedule_ready =================================================

void player_t::schedule_ready( double delta_time,
                               bool   waiting )
{
  assert( ! readying );

  executing = 0;
  channeling = 0;
  action_queued = false;

  double gcd_adjust = gcd_ready - ( sim -> current_time + delta_time );
  if ( gcd_adjust > 0 ) delta_time += gcd_adjust;

  if ( waiting )
  {
    total_waiting += delta_time;
  }
  else
  {
    double lag = 0;

    if ( last_foreground_action )
    {
      if ( last_foreground_action -> trigger_gcd == 0 )
      {
        lag = 0;
      }
      else if ( last_foreground_action -> channeled )
      {
	double delta = sim -> channel_lag_stddev * 2.0;
	lag = rngs.lag_channel -> range( sim -> channel_lag - delta, sim -> channel_lag + delta );
      }
      else if ( gcd_adjust > 0 && last_foreground_action -> time_to_execute == 0 )
      {
	double delta = sim -> gcd_lag_stddev * 2.0;
        lag = rngs.lag_gcd -> range( sim -> gcd_lag - delta, sim -> gcd_lag + delta );
      }
      else // queued cast
      {
	double delta = sim -> queue_lag_stddev * 2.0;
        lag = rngs.lag_queue -> range( sim -> queue_lag - delta, sim -> queue_lag + delta );
	action_queued = true;
      }
    }

    if ( lag < 0 ) lag = 0;

    delta_time += lag;
  }

  if ( last_foreground_action )
  {
    last_foreground_action -> stats -> total_execute_time += delta_time;
  }

  if ( delta_time == 0 ) delta_time = 0.000001;

  readying = new ( sim ) player_ready_event_t( sim, this, delta_time );
}

// player_t::interrupt ======================================================

void player_t::interrupt()
{
  // FIXME! Players will need to override this to handle background repeating actions.

  if ( sim -> log ) log_t::output( sim, "%s is interrupted", name() );

  if ( executing  ) executing  -> cancel();
  if ( channeling ) channeling -> cancel();

  if ( buffs.stunned -> check() )
  {
    if ( readying ) event_t::cancel( readying );
  }
  else
  {
    if ( ! readying && ! sleeping ) schedule_ready();
  }
}

// player_t::clear_debuffs===================================================

void player_t::clear_debuffs()
{
  // FIXME! At the moment we are just clearing DoTs

  if ( sim -> log ) log_t::output( sim, "%s clears debuffs from %s", name(), sim -> target -> name() );

  for ( action_t* a = action_list; a; a = a -> next )
  {
    if ( a -> ticking ) a -> cancel();
  }
}

// player_t::execute_action =================================================

action_t* player_t::execute_action()
{
  readying = 0;

  action_t* action=0;

  for ( action = action_list; action; action = action -> next )
  {
    if ( action -> background )
      continue;

    if ( action -> ready() )
      break;

    if ( action -> wait_on_ready == 1 )
    {
      action = 0;
      break;
    }
  }

  last_foreground_action = action;

  if ( action )
  {
    action -> schedule_execute();
  }

  return action;
}

// player_t::regen =========================================================

void player_t::regen( double periodicity )
{
  int resource_type = primary_resource();

  if ( sim -> infinite_resource[ resource_type ] == 0 )
  {
    if ( resource_type == RESOURCE_ENERGY )
    {
      double energy_regen = periodicity * energy_regen_per_second;

      resource_gain( RESOURCE_ENERGY, energy_regen, gains.energy_regen );
    }
    else if ( resource_type == RESOURCE_FOCUS )
    {
      double focus_regen = periodicity * focus_regen_per_second;

      resource_gain( RESOURCE_FOCUS, focus_regen, gains.focus_regen );
    }
    else if ( resource_type == RESOURCE_MANA )
    {
      if( buffs.innervate -> up() )
      {
	resource_gain( RESOURCE_MANA, buffs.innervate -> current_value * periodicity, gains.innervate );
      }

      double spirit_regen = periodicity * sqrt( intellect() ) * spirit() * mana_regen_base;

      if ( recent_cast() && mana_regen_while_casting < 1.0 )
      {
        spirit_regen *= mana_regen_while_casting;
      }
      if( spirit_regen > 0 )
      {
	resource_gain( RESOURCE_MANA, spirit_regen, gains.spirit_intellect_regen );
      }

      double mp5_regen = periodicity * ( mp5 + intellect() * mp5_per_intellect ) / 5.0;

      resource_gain( RESOURCE_MANA, mp5_regen, gains.mp5_regen );

      if ( buffs.replenishment -> up() )
      {
        double replenishment_regen = periodicity * resource_max[ RESOURCE_MANA ] * 0.0020 / 1.0;

        resource_gain( RESOURCE_MANA, replenishment_regen, gains.replenishment );
      }

      double bow = buffs.blessing_of_wisdom -> current_value;
      double ms  = sim -> auras.mana_spring_totem -> current_value;

      if ( ms > bow )
      {
        double mana_spring_regen = periodicity * ms / 2.0;

        resource_gain( RESOURCE_MANA, mana_spring_regen, gains.mana_spring_totem );
      }
      else if ( bow > 0 )
      {
        double wisdom_regen = periodicity * bow / 5.0;

        resource_gain( RESOURCE_MANA, wisdom_regen, gains.blessing_of_wisdom );
      }
    }
  }

  if ( resource_type != RESOURCE_NONE )
  {
    int index = ( int ) sim -> current_time;
    int size = timeline_resource.size();

    if ( index >= size ) timeline_resource.insert( timeline_resource.begin() + size, size, 0 );

    timeline_resource[ index ] += resource_current[ resource_type ];
  }
}

// player_t::resource_loss =================================================

double player_t::resource_loss( int       resource,
                                double    amount,
                                action_t* action )
{
  if ( amount == 0 ) return 0;

  double actual_amount;

  if ( sim -> infinite_resource[ resource ] == 0 )
  {
    resource_current[ resource ] -= amount;
    actual_amount = amount;
  }
  else
  {
    actual_amount = 0;
  }

  resource_lost[ resource ] += amount;

  if ( resource == RESOURCE_MANA )
  {
    last_cast = sim -> current_time;
  }

  if ( action ) action_callback_t::trigger( resource_loss_callbacks[ resource ], action );

  if ( sim -> debug ) log_t::output( sim, "Player %s loses %.0f %s", name(), amount, util_t::resource_type_string( resource ) );

  return actual_amount;
}

// player_t::resource_gain =================================================

double player_t::resource_gain( int       resource,
                                double    amount,
                                gain_t*   source,
                                action_t* action )
{
  if ( sleeping ) return 0;

  double actual_amount = std::min( amount, resource_max[ resource ] - resource_current[ resource ] );
  if ( actual_amount > 0 )
  {
    resource_current[ resource ] += actual_amount;
  }

  resource_gained [ resource ] += actual_amount;

  if ( source ) source -> add( actual_amount, amount - actual_amount );

  if ( action ) action_callback_t::trigger( resource_gain_callbacks[ resource ], action );

  if ( sim -> log )
  {
    log_t::output( sim, "%s gains %.0f (%.0f) %s from %s (%.0f)",
                   name(), actual_amount, amount,
                   util_t::resource_type_string( resource ), source ? source -> name() : "unknown",
                   resource_current[ resource ] );

    if ( amount && source )
    {
      log_t::resource_gain_event( this, resource, amount, actual_amount, source );
    }
  }

  return actual_amount;
}

// player_t::resource_available ============================================

bool player_t::resource_available( int    resource,
                                   double cost ) SC_CONST
{
  if ( resource == RESOURCE_NONE || cost == 0 )
  {
    return true;
  }

  return resource_current[ resource ] >= cost;
}

// player_t::stat_gain ======================================================

void player_t::stat_gain( int    stat,
                          double amount )
{
  if ( sim -> log ) log_t::output( sim, "%s gains %.0f %s", name(), amount, util_t::stat_type_string( stat ) );

  switch ( stat )
  {
  case STAT_STRENGTH:  attribute[ ATTR_STRENGTH  ] += amount; break;
  case STAT_AGILITY:   attribute[ ATTR_AGILITY   ] += amount; break;
  case STAT_STAMINA:   attribute[ ATTR_STAMINA   ] += amount; break;
  case STAT_INTELLECT: attribute[ ATTR_INTELLECT ] += amount; break;
  case STAT_SPIRIT:    attribute[ ATTR_SPIRIT    ] += amount; break;

  case STAT_MAX: for( int i=0; i < ATTRIBUTE_MAX; i++ ) attribute[ i ] += amount; break;

  case STAT_HEALTH: resource_gain( RESOURCE_HEALTH, amount ); break;
  case STAT_MANA:   resource_gain( RESOURCE_MANA,   amount ); break;
  case STAT_RAGE:   resource_gain( RESOURCE_RAGE,   amount ); break;
  case STAT_ENERGY: resource_gain( RESOURCE_ENERGY, amount ); break;
  case STAT_FOCUS:  resource_gain( RESOURCE_FOCUS,  amount ); break;
  case STAT_RUNIC:  resource_gain( RESOURCE_RUNIC,  amount ); break;

  case STAT_SPELL_POWER:       spell_power[ SCHOOL_MAX ] += amount; break;
  case STAT_SPELL_PENETRATION: spell_penetration         += amount; break;
  case STAT_MP5:               mp5                       += amount; break;

  case STAT_ATTACK_POWER:             attack_power       += amount;                            break;
  case STAT_EXPERTISE_RATING:         attack_expertise   += amount / rating.expertise;         break;
  case STAT_ARMOR_PENETRATION_RATING: attack_penetration += amount / rating.armor_penetration; break;

  case STAT_HIT_RATING:
    attack_hit += amount / rating.attack_hit;
    spell_hit  += amount / rating.spell_hit;
    break;

  case STAT_CRIT_RATING:
    attack_crit += amount / rating.attack_crit;
    spell_crit  += amount / rating.spell_crit;
    break;

  case STAT_HASTE_RATING: haste_rating += amount; recalculate_haste(); break;

  case STAT_ARMOR: armor += amount; break;

  default: assert( 0 );
  }
}

// player_t::stat_loss ======================================================

void player_t::stat_loss( int    stat,
                          double amount )
{
  if ( sim -> log ) log_t::output( sim, "%s loses %.0f %s", name(), amount, util_t::stat_type_string( stat ) );

  switch ( stat )
  {
  case STAT_STRENGTH:  attribute[ ATTR_STRENGTH  ] -= amount; break;
  case STAT_AGILITY:   attribute[ ATTR_AGILITY   ] -= amount; break;
  case STAT_STAMINA:   attribute[ ATTR_STAMINA   ] -= amount; break;
  case STAT_INTELLECT: attribute[ ATTR_INTELLECT ] -= amount; break;
  case STAT_SPIRIT:    attribute[ ATTR_SPIRIT    ] -= amount; break;

  case STAT_MAX: for( int i=0; i < ATTRIBUTE_MAX; i++ ) attribute[ i ] -= amount; break;

  case STAT_HEALTH: resource_loss( RESOURCE_HEALTH, amount ); break;
  case STAT_MANA:   resource_loss( RESOURCE_MANA,   amount ); break;
  case STAT_RAGE:   resource_loss( RESOURCE_RAGE,   amount ); break;
  case STAT_ENERGY: resource_loss( RESOURCE_ENERGY, amount ); break;
  case STAT_FOCUS:  resource_loss( RESOURCE_FOCUS,  amount ); break;
  case STAT_RUNIC:  resource_loss( RESOURCE_RUNIC,  amount ); break;

  case STAT_SPELL_POWER:       spell_power[ SCHOOL_MAX ] -= amount; break;
  case STAT_SPELL_PENETRATION: spell_penetration         -= amount; break;
  case STAT_MP5:               mp5                       -= amount; break;

  case STAT_ATTACK_POWER:             attack_power       -= amount;                            break;
  case STAT_EXPERTISE_RATING:         attack_expertise   -= amount / rating.expertise;         break;
  case STAT_ARMOR_PENETRATION_RATING: attack_penetration -= amount / rating.armor_penetration; break;

  case STAT_HIT_RATING:
    attack_hit -= amount / rating.attack_hit;
    spell_hit  -= amount / rating.spell_hit;
    break;

  case STAT_CRIT_RATING:
    attack_crit -= amount / rating.attack_crit;
    spell_crit  -= amount / rating.spell_crit;
    break;

  case STAT_HASTE_RATING: haste_rating -= amount; recalculate_haste(); break;

  case STAT_ARMOR: armor -= amount; break;

  default: assert( 0 );
  }
}

// player_t::summon_pet =====================================================

void player_t::summon_pet( const char* pet_name, 
			   double      duration )
{
  for ( pet_t* p = pet_list; p; p = p -> next_pet )
  {
    if ( p -> name_str == pet_name )
    {
      p -> summon( duration );
      return;
    }
  }
  util_t::printf( "\nsimcraft: Player %s is unable to summon pet '%s'\n", name(), pet_name );
  assert( 0 );
}

// player_t::dismiss_pet ====================================================

void player_t::dismiss_pet( const char* pet_name )
{
  for ( pet_t* p = pet_list; p; p = p -> next_pet )
  {
    if ( p -> name_str == pet_name )
    {
      p -> dismiss();
      return;
    }
  }
  assert( 0 );
}

// player_t::register_callbacks =============================================

void player_t::register_callbacks()
{
  if ( primary_resource() == RESOURCE_MANA )
  {
    action_callback_t* cb = new judgement_of_wisdom_callback_t( this );

    register_attack_result_callback       ( RESULT_HIT_MASK, cb );
    register_spell_result_callback        ( RESULT_HIT_MASK, cb );
    register_attack_direct_result_callback( RESULT_HIT_MASK, cb );
    register_spell_direct_result_callback ( RESULT_HIT_MASK, cb );
  }
}

// player_t::register_resource_gain_callback ================================

void player_t::register_resource_gain_callback( int                resource,
                                                action_callback_t* cb )
{
  resource_gain_callbacks[ resource ].push_back( cb );
}

// player_t::register_resource_loss_callback ================================

void player_t::register_resource_loss_callback( int                resource,
                                                action_callback_t* cb )
{
  resource_loss_callbacks[ resource ].push_back( cb );
}

// player_t::register_attack_result_callback ================================

void player_t::register_attack_result_callback( int                mask,
                                                action_callback_t* cb )
{
  for ( int i=0; i < RESULT_MAX; i++ )
  {
    if ( mask < 0 || ( mask & ( 1 << i ) ) )
    {
      attack_result_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::register_spell_result_callback =================================

void player_t::register_spell_result_callback( int                mask,
                                               action_callback_t* cb )
{
  for ( int i=0; i < RESULT_MAX; i++ )
  {
    if ( mask < 0 || ( mask & ( 1 << i ) ) )
    {
      spell_result_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::register_attack_direct_result_callback ================================

void player_t::register_attack_direct_result_callback( int                mask,
                                                       action_callback_t* cb )
{
  for ( int i=0; i < RESULT_MAX; i++ )
  {
    if ( mask < 0 || ( mask & ( 1 << i ) ) )
    {
      attack_direct_result_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::register_spell_direct_result_callback =================================

void player_t::register_spell_direct_result_callback( int                mask,
                                                      action_callback_t* cb )
{
  for ( int i=0; i < RESULT_MAX; i++ )
  {
    if ( mask < 0 || ( mask & ( 1 << i ) ) )
    {
      spell_direct_result_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::register_tick_callback =========================================

void player_t::register_tick_callback( action_callback_t* cb )
{
  tick_callbacks.push_back( cb );
}

// player_t::register_tick_damage_callback ==================================

void player_t::register_tick_damage_callback( action_callback_t* cb )
{
  tick_damage_callbacks.push_back( cb );
}

// player_t::register_direct_damage_callback ================================

void player_t::register_direct_damage_callback( action_callback_t* cb )
{
  direct_damage_callbacks.push_back( cb );
}

// player_t::share_cooldown =================================================

void player_t::share_cooldown( const std::string& group,
                               double             duration )
{
  double ready = sim -> current_time + duration;

  for ( action_t* a = action_list; a; a = a -> next )
  {
    if ( group == a -> cooldown_group )
    {
      if ( ready > a -> cooldown_ready )
      {
        a -> cooldown_ready = ready;
      }
    }
  }
}

// player_t::share_duration =================================================

void player_t::share_duration( const std::string& group,
                               double             ready )
{
  for ( action_t* a = action_list; a; a = a -> next )
  {
    if ( a -> duration_group == group )
    {
      if ( a -> duration_ready < ready )
      {
        a -> duration_ready = ready;
      }
    }
  }
}

// player_t::recalculate_haste ==============================================

void player_t::recalculate_haste()
{
  spell_haste = 1.0 / ( 1.0 + haste_rating / rating. spell_haste );
  attack_haste = 1.0 / ( 1.0 + haste_rating / rating.attack_haste );
}

// player_t::recent_cast ====================================================

bool player_t::recent_cast()
{
  return ( last_cast > 0 ) && ( ( last_cast + 5.0 ) > sim -> current_time );
}

// player_t::find_action ====================================================

action_t* player_t::find_action( const std::string& str )
{
  for ( action_t* a = action_list; a; a = a -> next )
    if ( str == a -> name_str )
      return a;

  return 0;
}

// player_t::aura_gain ======================================================

void player_t::aura_gain( const char* aura_name , int aura_id )
{
  if ( sim -> log && ! sleeping )
  {
    log_t::output( sim, "%s gains %s", name(), aura_name );
    log_t::aura_gain_event( this, aura_name, aura_id );
  }

}

// player_t::aura_loss ======================================================

void player_t::aura_loss( const char* aura_name , int aura_id )
{
  if ( sim -> log && ! sleeping )
  {
    log_t::output( sim, "%s loses %s", name(), aura_name );
    log_t::aura_loss_event( this, aura_name, aura_id );
  }
}

// player_t::get_gain =======================================================

gain_t* player_t::get_gain( const std::string& name )
{
  gain_t* g=0;

  for ( g = gain_list; g; g = g -> next )
  {
    if ( g -> name_str == name )
      return g;
  }

  g = new gain_t( name );

  gain_t** tail = &gain_list;

  while ( *tail && name > ( ( *tail ) -> name_str ) )
  {
    tail = &( ( *tail ) -> next );
  }

  g -> next = *tail;
  *tail = g;

  return g;
}

// player_t::get_proc =======================================================

proc_t* player_t::get_proc( const std::string& name,
                            sim_t*             sim )
{
  proc_t* p=0;

  for ( p = proc_list; p; p = p -> next )
  {
    if ( p -> name_str == name )
      return p;
  }

  p = new proc_t( sim, name );

  proc_t** tail = &proc_list;

  while ( *tail && name > ( ( *tail ) -> name_str ) )
  {
    tail = &( ( *tail ) -> next );
  }

  p -> next = *tail;
  *tail = p;

  return p;
}

// player_t::get_stats ======================================================

stats_t* player_t::get_stats( const std::string& n )
{
  stats_t* stats=0;

  for ( stats = stats_list; stats; stats = stats -> next )
  {
    if ( stats -> name_str == n )
      return stats;
  }

  if ( ! stats )
  {
    stats = new stats_t( n, this );
    stats -> init();
    stats_t** tail= &stats_list;
    while ( *tail && n > ( ( *tail ) -> name_str ) )
    {
      tail = &( ( *tail ) -> next );
    }
    stats -> next = *tail;
    *tail = stats;
  }

  return stats;
}

// player_t::get_uptime =====================================================

uptime_t* player_t::get_uptime( const std::string& name )
{
  uptime_t* u=0;

  for ( u = uptime_list; u; u = u -> next )
  {
    if ( u -> name_str == name )
      return u;
  }

  u = new uptime_t( name );

  uptime_t** tail = &uptime_list;

  while ( *tail && name > ( ( *tail ) -> name_str ) )
  {
    tail = &( ( *tail ) -> next );
  }

  u -> next = *tail;
  *tail = u;

  return u;
}

// player_t::get_rng =======================================================

rng_t* player_t::get_rng( const std::string& n, int type )
{
  assert( sim -> rng );

  if ( ! sim -> smooth_rng || type == RNG_GLOBAL ) return sim -> rng;

  if ( type == RNG_DETERMINISTIC ) return sim -> deterministic_rng;

  rng_t* rng=0;

  for ( rng = rng_list; rng; rng = rng -> next )
  {
    if ( rng -> name_str == n )
      return rng;
  }

  if ( ! rng )
  {
    rng = rng_t::create( sim, n, type );
    rng -> next = rng_list;
    rng_list = rng;
  }

  return rng;
}

// ===== Racial Abilities ==================================================

// Arcane Torrent ==========================================================

struct arcane_torrent_t : public action_t
{
  arcane_torrent_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "arcane_torrent", player )
  {
    trigger_gcd = 0;
    cooldown    = 120;
  }

  virtual void execute()
  {
    int resource = player -> primary_resource();
    double gain = 0;
    switch( resource )
    {
      case RESOURCE_MANA:
        gain = player -> resource_max [ RESOURCE_MANA ] * 0.06;
        break;
      case RESOURCE_ENERGY:
      case RESOURCE_RUNIC:
        gain = 15;
        break;
      default:
        break;
    }

    if( gain > 0 )
      player -> resource_gain( resource, gain, player -> gains.arcane_torrent );

    update_ready();
  }

  virtual bool ready()
  {
    if( ! action_t::ready() )
      return false;

    if ( player -> race != RACE_BLOOD_ELF )
      return false;

    int resource = player -> primary_resource();
    switch( resource )
    {
      case RESOURCE_MANA:
        if( player -> resource_current [ resource ] / player -> resource_max [ resource ] <= 0.94 )
          return true;
        break;
      case RESOURCE_ENERGY:
      case RESOURCE_RUNIC:
        if( player -> resource_max [ resource ] - player -> resource_current [ resource ] <= 15 )
          return true;
        break;
      default:
        break;
    }

    return false;
  }
};

// Berserking ==========================================================

struct berserking_t : public action_t
{
  berserking_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "berserking", player )
  {
    trigger_gcd = 0;
    cooldown    = 180;
  }

  virtual void execute()
  {
    if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), name() );

    update_ready();

    player -> buffs.berserking -> trigger();
  }

  virtual bool ready()
  {
    if( ! action_t::ready() )
      return false;

    if ( player -> race != RACE_TROLL )
      return false;

    return true;
  }
};

// Blood Fury ==========================================================

struct blood_fury_t : public action_t
{
  blood_fury_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "blood_fury", player )
  {
    trigger_gcd = 0;
    cooldown    = 120;
  }

  virtual void execute()
  {
    if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), name() );

    update_ready();

    if ( player -> type == WARRIOR || player -> type == ROGUE || player -> type == DEATH_KNIGHT ||
         player -> type == HUNTER  || player -> type == SHAMAN )
    {
      player -> buffs.blood_fury_ap -> trigger();
    }

    if ( player -> type == SHAMAN  || player -> type == WARLOCK )
    {
      player -> buffs.blood_fury_sp -> trigger();
    }
  }

  virtual bool ready()
  {
    if( ! action_t::ready() )
      return false;

    if ( player -> race != RACE_ORC )
      return false;

    return true;
  }
};

// Stoneform ==========================================================

struct stoneform_t : public action_t
{
  stoneform_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "stoneform", player )
  {
    trigger_gcd = 0;
    cooldown    = 120;
  }

  virtual void execute()
  {
    if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), name() );

    update_ready();

    player -> buffs.stoneform -> trigger();
  }

  virtual bool ready()
  {
    if( ! action_t::ready() )
      return false;

    if ( player -> race != RACE_DWARF )
      return false;

    return true;
  }
};

// Cycle Action ============================================================

struct cycle_t : public action_t
{
  action_t* current_action;

  cycle_t( player_t* player, const std::string& options_str ) :
      action_t( ACTION_OTHER, "cycle", player ), current_action( 0 )
  {}

  virtual void reset()
  {
    action_t::reset();

    if ( ! current_action )
    {
      current_action = next;
      if ( ! current_action )
      {
        util_t::printf( "simcraft: player %s has no actions after 'cycle'\n", player -> name() );
        exit( 0 );
      }
      for ( action_t* a = next; a; a = a -> next ) a -> background = true;
    }
  }

  virtual void schedule_execute()
  {
    player -> last_foreground_action = current_action;
    current_action -> schedule_execute();
    current_action = current_action -> next;
    if ( ! current_action ) current_action = next;
  }

  virtual bool ready()
  {
    if ( ! current_action ) return false;

    return current_action -> ready();
  }
};

// Restart Sequence Action =================================================

struct restart_sequence_t : public action_t
{
  std::string seq_name_str;

  restart_sequence_t( player_t* player, const std::string& options_str ) :
      action_t( ACTION_OTHER, "restart_sequence", player )
  {
    option_t options[] =
    {
      { "name", OPT_STRING, &seq_name_str },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    trigger_gcd = 0;
  }

  virtual void execute()
  {
    for ( action_t* a = player -> action_list; a; a = a -> next )
    {
      if ( a -> type != ACTION_SEQUENCE )
        continue;

      if ( ! seq_name_str.empty() )
        if ( seq_name_str != a -> name_str )
          continue;

      ( ( sequence_t* ) a ) -> restart();
    }
  }
};

// Restore Mana Action =====================================================

struct restore_mana_t : public action_t
{
  double mana;

  restore_mana_t( player_t* player, const std::string& options_str ) :
      action_t( ACTION_OTHER, "restore_mana", player ), mana( 0 )
  {
    option_t options[] =
    {
      { "mana", OPT_FLT, &mana },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    trigger_gcd = 0;
  }

  virtual void execute()
  {
    double mana_missing = player -> resource_max[ RESOURCE_MANA ] - player -> resource_current[ RESOURCE_MANA ];
    double mana_gain = mana;

    if ( mana_gain == 0 || mana_gain > mana_missing ) mana_gain = mana_missing;

    if ( mana_gain > 0 )
    {
      player -> resource_gain( RESOURCE_MANA, mana_gain, player -> gains.restore_mana );
    }
  }
};

// Wait Until Ready Action ===================================================

struct wait_until_ready_t : public action_t
{
  double sec;

  wait_until_ready_t( player_t* player, const std::string& options_str ) :
      action_t( ACTION_OTHER, "wait", player ), sec( 1.0 )
  {
    option_t options[] =
    {
      { "sec", OPT_FLT, &sec },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );
  }

  virtual void execute()
  {
    trigger_gcd = sec;

    for ( action_t* a = player -> action_list; a; a = a -> next )
    {
      if ( a -> background ) continue;

      if ( a -> cooldown_ready > sim -> current_time )
      {
        double delta_time = a -> cooldown_ready - sim -> current_time;
        if ( delta_time < trigger_gcd ) trigger_gcd = delta_time;
      }

      if ( a -> duration_ready > sim -> current_time )
      {
        double delta_time = a -> duration_ready - ( sim -> current_time + a -> execute_time() );
        if ( delta_time < trigger_gcd ) trigger_gcd = delta_time;
      }
    }

    player -> total_waiting += trigger_gcd;
  }
};

// Use Item Action ===========================================================

struct use_item_t : public action_t
{
  item_t* item;
  spell_t* discharge;
  action_callback_t* trigger;
  stat_buff_t* buff;
  std::string use_name;

  use_item_t( player_t* player, const std::string& options_str ) :
      action_t( ACTION_OTHER, "use_item", player ),
      item(0), discharge(0), trigger(0), buff(0)
  {
    std::string item_name;
    option_t options[] =
    {
      { "name", OPT_STRING, &item_name },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    if ( item_name.empty() )
    {
      util_t::printf( "simcraft: Player %s has 'use_item' action with no 'name=' option.\n", player -> name() );
      assert( false );
    }

    item = player -> find_item( item_name );
    if ( ! item )
    {
      util_t::printf( "simcraft: Player %s attempting 'use_item' action with item '%s' which is not currently equipped.\n", player -> name(), item_name.c_str() );
      assert( false );
    }
    if ( ! item -> use.active() )
    {
      util_t::printf( "simcraft: Player %s attempting 'use_item' action with item '%s' which has no 'use=' encoding.\n", player -> name(), item_name.c_str() );
      assert( false );
    }

    item_t::special_effect_t& e = item -> use;

    use_name = e.name_str.empty() ? item_name : e.name_str;

    if ( e.trigger_type )
    {
      if ( e.stat )
      {
        trigger = unique_gear_t::register_stat_proc( e.trigger_type, e.trigger_mask, use_name, player,
                                                     e.stat, e.max_stacks, e.amount, e.proc_chance, 0, 0 );
      }
      else if ( e.school )
      {
        trigger = unique_gear_t::register_discharge_proc( e.trigger_type, e.trigger_mask, use_name, player,
                                                          e.max_stacks, e.school, e.amount, e.amount, e.proc_chance, 0 );
      }

      if ( trigger ) trigger -> deactivate();
    }
    else if ( e.school )
    {
      struct discharge_spell_t : public spell_t
      {
        discharge_spell_t( const char* n, player_t* p, double a, int s ) :
            spell_t( n, p, RESOURCE_NONE, s )
        {
          trigger_gcd = 0;
          base_dd_min = a;
          base_dd_max = a;
          may_crit    = true;
          background  = true;
          base_spell_power_multiplier = 0;
          reset();
        }
      };

      discharge = new discharge_spell_t( use_name.c_str(), player, e.amount, e.school );
    }
    else if ( e.stat )
    {
      if( e.max_stacks  <= 0 ) e.max_stacks  = 1;
      if( e.proc_chance <= 0 ) e.proc_chance = 1;
      
      buff = new stat_buff_t( player, use_name, e.stat, e.amount, e.max_stacks, e.duration, 0, e.proc_chance );
    }
    else assert( false );

    cooldown = item -> use.cooldown;
    trigger_gcd = 0;
  }

  virtual void execute()
  {
    if ( discharge )
    {
      discharge -> execute();
    }
    else if ( trigger )
    {
      if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), use_name.c_str() );

      trigger -> activate();

      if ( item -> use.duration )
      {
        struct trigger_expiration_t : public event_t
        {
          item_t* item;
          action_callback_t* trigger;

          trigger_expiration_t( sim_t* sim, player_t* player, item_t* i, action_callback_t* t ) : event_t( sim, player ), item( i ), trigger( t )
          {
            name = item -> name();
            sim -> add_event( this, item -> use.duration );
          }
          virtual void execute()
          {
            trigger -> deactivate();
          }
        };

        new ( sim ) trigger_expiration_t( sim, player, item, trigger );
      }
    }
    else if( buff )
    {
      if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), use_name.c_str() );

      buff -> trigger();
    }
    else assert( false );

    update_ready();
  }

  virtual void reset()
  {
    action_t::reset();
    if ( trigger ) trigger -> deactivate();
  }
};

// player_t::create_action ==================================================

action_t* player_t::create_action( const std::string& name,
                                   const std::string& options_str )
{
  if ( name == "arcane_torrent"   ) return new   arcane_torrent_t( this, options_str );
  if ( name == "berserking"       ) return new       berserking_t( this, options_str );
  if ( name == "blood_fury"       ) return new       blood_fury_t( this, options_str );
  if ( name == "cycle"            ) return new            cycle_t( this, options_str );
  if ( name == "restart_sequence" ) return new restart_sequence_t( this, options_str );
  if ( name == "restore_mana"     ) return new     restore_mana_t( this, options_str );
  if ( name == "stoneform"        ) return new        stoneform_t( this, options_str );
  if ( name == "use_item"         ) return new         use_item_t( this, options_str );
  if ( name == "wait"             ) return new wait_until_ready_t( this, options_str );

  return consumable_t::create_action( this, name, options_str );
}

// player_t::find_pet =======================================================

pet_t* player_t::find_pet( const std::string& pet_name )
{
  for ( pet_t* p = pet_list; p; p = p -> next_pet )
    if ( p -> name_str == pet_name )
      return p;

  return 0;
}

// player_t::trigger_replenishment ================================================

void player_t::trigger_replenishment()
{
  if ( sim -> overrides.replenishment )
    return;

  if ( sim -> replenishment_targets > 0 )
  {
    choose_replenish_targets( this );
  }
  else
  {
    replenish_raid( this );
  }
}


// player_t::get_talent_translation ===============================================

bool player_t::get_talent_translation( std::vector<int*>& tree,
                                       talent_translation_t translation[] )
{
  for ( int i=0; translation[ i ].index > 0; i++ ) tree.push_back( translation[ i ].address );

  return true;
}

bool player_t::get_talent_translation( std::vector<int*>& tree1,
                                       std::vector<int*>& tree2,
                                       std::vector<int*>& tree3,
                                       talent_translation_t translation[][3] )
{
  for ( int i=0; translation[ i ][ 0 ].index > 0; i++ ) tree1.push_back( translation[ i ][ 0 ].address );
  for ( int i=0; translation[ i ][ 1 ].index > 0; i++ ) tree2.push_back( translation[ i ][ 1 ].address );
  for ( int i=0; translation[ i ][ 2 ].index > 0; i++ ) tree3.push_back( translation[ i ][ 2 ].address );

  return true;
}

// player_t::get_talent_trees ===============================================

bool player_t::get_talent_trees( std::vector<int*>& tree1,
                                 std::vector<int*>& tree2,
                                 std::vector<int*>& tree3 )
{
  return false;
}

// player_t::parse_talent_tree ==============================================

bool player_t::parse_talent_tree( std::vector<int*>& talent_tree,
                                  const std::string& talent_string )
{
  const char* s = talent_string.c_str();

  unsigned int size = std::min( talent_tree.size(), talent_string.size() );

  for ( unsigned int i=0; i < size; i++ )
  {
    int* address = talent_tree[ i ];
    if ( ! address ) continue;
    char c = s[ i ];
    if ( c < '0' || c > '5' )
    {
      util_t::printf( "\nsimcraft: Player %s has illegal character '%c' in talent encoding.\n", name(), c );
      return false;
    }
    *address = c - '0';
  }

  return true;
}

// player_t::parse_talents_armory ===========================================

bool player_t::parse_talents_armory( const std::string& talent_string )
{
  std::vector<int*> talent_tree1, talent_tree2, talent_tree3;

  if ( ! get_talent_trees( talent_tree1, talent_tree2, talent_tree3 ) )
    return false;

  int size1 = talent_tree1.size();
  int size2 = talent_tree2.size();
  int size3 = talent_tree3.size();

  std::string buffer = talent_string;

  int total_size = ( size1 + size2 + size3 );
  if ( ( total_size - buffer.size() ) > 0 )
  {
    buffer.resize( total_size, '0' );
  }

  std::string talent_string1( buffer,     0,  size1 );
  std::string talent_string2( buffer, size1,  size2 );
  std::string talent_string3( buffer, size1 + size2 );

  return( parse_talent_tree( talent_tree1, talent_string1 ) &&
          parse_talent_tree( talent_tree2, talent_string2 ) &&
          parse_talent_tree( talent_tree3, talent_string3 ) );
}

// player_t::parse_talents_mmo ==============================================

bool player_t::parse_talents_mmo( const std::string& talent_string )
{
  return parse_talents_armory( talent_string );
}

// player_t::parse_talents_wowhead ==========================================

bool player_t::parse_talents_wowhead( const std::string& talent_string )
{
  // wowhead format: [tree_1]Z[tree_2]Z[tree_3] where the trees are character encodings
  // each character expands to a pair of numbers [0-5][0-5]
  // unused deeper talents are simply left blank instead of filling up the string with zero-zero encodings

  struct decode_t
  {
    char key, first, second;
  }
  decoding[] =
  {
    { '0', '0', '0' },  { 'z', '0', '1' },  { 'M', '0', '2' },  { 'c', '0', '3' }, { 'm', '0', '4' },  { 'V', '0', '5' },
    { 'o', '1', '0' },  { 'k', '1', '1' },  { 'R', '1', '2' },  { 's', '1', '3' }, { 'a', '1', '4' },  { 'q', '1', '5' },
    { 'b', '2', '0' },  { 'd', '2', '1' },  { 'r', '2', '2' },  { 'f', '2', '3' }, { 'w', '2', '4' },  { 'i', '2', '5' },
    { 'h', '3', '0' },  { 'u', '3', '1' },  { 'G', '3', '2' },  { 'I', '3', '3' }, { 'N', '3', '4' },  { 'A', '3', '5' },
    { 'L', '4', '0' },  { 'p', '4', '1' },  { 'T', '4', '2' },  { 'j', '4', '3' }, { 'n', '4', '4' },  { 'y', '4', '5' },
    { 'x', '5', '0' },  { 't', '5', '1' },  { 'g', '5', '2' },  { 'e', '5', '3' }, { 'v', '5', '4' },  { 'E', '5', '5' },
    { '\0', '\0', '\0' }
  };

  std::vector<int*> talent_trees[ 3 ];
  unsigned int tree_size[ 3 ];

  if ( ! get_talent_trees( talent_trees[ 0 ], talent_trees[ 1 ] , talent_trees[ 2 ] ) )
    return false;

  for ( int i=0; i < 3; i++ )
  {
    tree_size[ i ] = talent_trees[ i ].size();
  }

  std::string talent_strings[ 3 ];
  int tree=0;

  for ( unsigned int i=1; i < talent_string.length(); i++ )
  {
    if ( tree > 2 )
    {
      util_t::fprintf( sim -> output_file, "Malformed wowhead talent string. Too many trees specified.\n" );
      assert( 0 );
    }

    char c = talent_string[ i ];

    if ( c == ':' ) break; // glyph encoding follows

    if ( c == 'Z' )
    {
      tree++;
      continue;
    }

    decode_t* decode = 0;
    for ( int j=0; decoding[ j ].key != '\0' && ! decode; j++ )
    {
      if ( decoding[ j ].key == c ) decode = decoding + j;
    }

    if ( ! decode )
    {
      util_t::fprintf( sim -> output_file, "Malformed wowhead talent string. Translation for '%c' unknown.\n", c );
      assert( 0 );
    }

    talent_strings[ tree ] += decode -> first;
    talent_strings[ tree ] += decode -> second;

    if ( talent_strings[ tree ].size() >= tree_size[ tree ] )
      tree++;
  }

  if ( sim -> debug )
  {
    util_t::fprintf( sim -> output_file, "%s tree1: %s\n", name(), talent_strings[ 0 ].c_str() );
    util_t::fprintf( sim -> output_file, "%s tree2: %s\n", name(), talent_strings[ 1 ].c_str() );
    util_t::fprintf( sim -> output_file, "%s tree3: %s\n", name(), talent_strings[ 2 ].c_str() );
  }

  for ( int i=0; i < 3; i++ )
  {
    if ( ! parse_talent_tree( talent_trees[ i ], talent_strings[ i ] ) )
      return false;
  }

  return true;
}

// player_t::save ===========================================================

bool player_t::save( FILE* file, int save_type )
{
  if ( save_type == SAVE_ALL )
  {
    util_t::fprintf( file, "#!simcraft\n\n" );
  }

  if ( !comment_str.empty() )
  {
    util_t::fprintf( file, "# %s\n", comment_str.c_str() );
  }

  if ( save_type == SAVE_ALL )
  {
    util_t::fprintf( file, "%s=%s\n", util_t::player_type_string( type ), name() );
    util_t::fprintf( file, "origin=%s\n", origin_str.c_str() );
    util_t::fprintf( file, "level=%d\n", level );
    util_t::fprintf( file, "race=%s\n", race_str.c_str() );

    if ( professions_str.size() > 0 )
    {
      util_t::fprintf( file, "professions=%s\n", professions_str.c_str() );
    };
  }

  if ( save_type == SAVE_ALL || save_type == SAVE_TALENTS )
  {
    if ( talents_str.size() > 0 )
    {
      util_t::fprintf( file, "talents=%s\n", talents_str.c_str() );
    };
    if ( glyphs_str.size() > 0 )
    {
      util_t::fprintf( file, "glyphs=%s\n", glyphs_str.c_str() );
    }
  }

  if ( save_type == SAVE_ALL || save_type == SAVE_ACTIONS )
  {
    if ( action_list_str.size() > 0 )
    {
      std::vector<std::string> splits;
      int num_splits = util_t::string_split( splits, action_list_str, "/" );
      for ( int i=0; i < num_splits; i++ )
      {
        util_t::fprintf( file, "actions%s%s\n", ( i ? "+=/" : "=" ), splits[ i ].c_str() );
      }
    }
  }

  if ( save_type == SAVE_ALL || save_type == SAVE_GEAR )
  {
    for ( int i=0; i < SLOT_MAX; i++ )
    {
      item_t& item = items[ i ];

      if ( item.active() )
      {
        util_t::fprintf( file, "%s=%s\n", item.slot_name(), item.options_str.c_str() );
      }
    }
    if ( ! items_str.empty() )
    {
      util_t::fprintf( file, "items=%s\n", items_str.c_str() );
    }

    util_t::fprintf( file, "# Gear Summary\n" );
    for ( int i=0; i < STAT_MAX; i++ )
    {
      double value = stats.get_stat( i );
      if ( value != 0 ) util_t::fprintf( file, "# gear_%s=%.0f\n", util_t::stat_type_string( i ), value );
    }
    if ( meta_gem != META_GEM_NONE ) util_t::fprintf( file, "# meta_gem=%s\n", util_t::meta_gem_type_string( meta_gem ) );
    if ( set_bonus.tier7_2pc() ) util_t::fprintf( file, "# tier7_2pc=1\n" );
    if ( set_bonus.tier7_4pc() ) util_t::fprintf( file, "# tier7_4pc=1\n" );
    if ( set_bonus.tier8_2pc() ) util_t::fprintf( file, "# tier8_2pc=1\n" );
    if ( set_bonus.tier8_4pc() ) util_t::fprintf( file, "# tier8_4pc=1\n" );
    if ( set_bonus.tier9_2pc() ) util_t::fprintf( file, "# tier9_2pc=1\n" );
    if ( set_bonus.tier9_4pc() ) util_t::fprintf( file, "# tier9_4pc=1\n" );
    if ( set_bonus.tier10_2pc() ) util_t::fprintf( file, "# tier10_2pc=1\n" );
    if ( set_bonus.tier10_4pc() ) util_t::fprintf( file, "# tier10_4pc=1\n" );

    for ( int i=0; i < SLOT_MAX; i++ )
    {
      item_t& item = items[ i ];
      if ( ! item.active() ) continue;
      if ( item.unique || item.unique_enchant || ! item.encoded_weapon_str.empty() )
      {
        util_t::fprintf( file, "# %s=%s", item.slot_name(), item.name() );
        if ( ! item.encoded_weapon_str.empty() ) util_t::fprintf( file, ",weapon=%s", item.encoded_weapon_str.c_str() );
        if ( item.unique_enchant ) util_t::fprintf( file, ",enchant=%s", item.encoded_enchant_str.c_str() );
        util_t::fprintf( file, "\n" );
      }
    }
  }

  return true;
}

// player_t::create_expression ====================================================
// -this is optional support for player specific expression functions or variables
// -if prefix.name.sufix is recognized, it needs to create "new" act_expression type
// -if name not recognized, returns 0
act_expression_t* player_t::create_expression( std::string& name,std::string& prefix,std::string& suffix, exp_res_t expected_type )
{
  act_expression_t* node=0;
  std::string e_name=name;
  if ( prefix!="" ) e_name=prefix+"."+e_name;
  if ( suffix!="" ) e_name=e_name+"."+suffix;
  if ( ( prefix=="buff" )&&( node==0 ) )
  {
    bool ex=( suffix!="value" )&&( suffix!="buff" )&&( suffix!="stacks" ); // if one of these, ignore expiration time
    if ( ( suffix=="" )&&( expected_type==ETP_BOOL ) ) ex=false; //also ignore expiration value if boolean result is needed
  }
  if ( ( prefix=="player" )&&( node==0 ) )
  {
    if ( name=="distance" )
    {
      node= new act_expression_t( AEXP_DOUBLE_PTR,e_name );
      node->p_value= &distance;
    }
  }
  return node;
}



// player_t::get_options ====================================================

std::vector<option_t>& player_t::get_options()
{
  if ( options.empty() )
  {
    option_t player_options[] =
    {
      // @option_doc loc=player/all/general title="General"
      { "name",                                 OPT_STRING,   &( name_str                                     ) },
      { "origin",                               OPT_STRING,   &( origin_str                                   ) },
      { "id",                                   OPT_STRING,   &( id_str                                       ) },
      { "talents",                              OPT_FUNC,     ( void* ) ::parse_talent_url                      },
      { "glyphs",                               OPT_STRING,   &( glyphs_str                                   ) },
      { "race",                                 OPT_STRING,   &( race_str                                     ) },
      { "level",                                OPT_INT,      &( level                                        ) },
      { "tank",                                 OPT_INT,      &( tank                                         ) },
      { "skill",                                OPT_FLT,      &( skill                                        ) },
      { "distance",                             OPT_FLT,      &( distance                                     ) },
      { "professions",                          OPT_STRING,   &( professions_str                              ) },
      { "actions",                              OPT_STRING,   &( action_list_str                              ) },
      { "actions+",                             OPT_APPEND,   &( action_list_str                              ) },
      { "sleeping",                             OPT_BOOL,     &( sleeping                                     ) },
      { "quiet",                                OPT_BOOL,     &( quiet                                        ) },
      { "save",                                 OPT_STRING,   &( save_str                                     ) },
      { "save_gear",                            OPT_STRING,   &( save_gear_str                                ) },
      { "save_talents",                         OPT_STRING,   &( save_talents_str                             ) },
      { "save_actions",                         OPT_STRING,   &( save_actions_str                             ) },
      { "comment",                              OPT_STRING,   &( comment_str                                  ) },
      // @option_doc loc=player/all/items title="Items"
      { "items",                                OPT_STRING,   &( items_str                                    ) },
      { "items+",                               OPT_APPEND,   &( items_str                                    ) },
      { "head",                                 OPT_STRING,   &( items[ SLOT_HEAD      ].options_str          ) },
      { "neck",                                 OPT_STRING,   &( items[ SLOT_NECK      ].options_str          ) },
      { "shoulders",                            OPT_STRING,   &( items[ SLOT_SHOULDERS ].options_str          ) },
      { "shirt",                                OPT_STRING,   &( items[ SLOT_SHIRT     ].options_str          ) },
      { "chest",                                OPT_STRING,   &( items[ SLOT_CHEST     ].options_str          ) },
      { "waist",                                OPT_STRING,   &( items[ SLOT_WAIST     ].options_str          ) },
      { "legs",                                 OPT_STRING,   &( items[ SLOT_LEGS      ].options_str          ) },
      { "feet",                                 OPT_STRING,   &( items[ SLOT_FEET      ].options_str          ) },
      { "wrists",                               OPT_STRING,   &( items[ SLOT_WRISTS    ].options_str          ) },
      { "hands",                                OPT_STRING,   &( items[ SLOT_HANDS     ].options_str          ) },
      { "finger1",                              OPT_STRING,   &( items[ SLOT_FINGER_1  ].options_str          ) },
      { "finger2",                              OPT_STRING,   &( items[ SLOT_FINGER_2  ].options_str          ) },
      { "trinket1",                             OPT_STRING,   &( items[ SLOT_TRINKET_1 ].options_str          ) },
      { "trinket2",                             OPT_STRING,   &( items[ SLOT_TRINKET_2 ].options_str          ) },
      { "back",                                 OPT_STRING,   &( items[ SLOT_BACK      ].options_str          ) },
      { "main_hand",                            OPT_STRING,   &( items[ SLOT_MAIN_HAND ].options_str          ) },
      { "off_hand",                             OPT_STRING,   &( items[ SLOT_OFF_HAND  ].options_str          ) },
      { "ranged",                               OPT_STRING,   &( items[ SLOT_RANGED    ].options_str          ) },
      { "tabard",                               OPT_STRING,   &( items[ SLOT_TABARD    ].options_str          ) },
      { "tier4_2pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T4_2PC ]                ) },
      { "tier4_4pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T4_4PC ]                ) },
      { "tier5_2pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T5_2PC ]                ) },
      { "tier5_4pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T5_4PC ]                ) },
      { "tier6_2pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T6_2PC ]                ) },
      { "tier6_4pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T6_4PC ]                ) },
      { "tier7_2pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T7_2PC ]                ) },
      { "tier7_4pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T7_4PC ]                ) },
      { "tier8_2pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T8_2PC ]                ) },
      { "tier8_4pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T8_4PC ]                ) },
      { "tier9_2pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T9_2PC ]                ) },
      { "tier9_4pc",                            OPT_BOOL,     &( set_bonus.count[ SET_T9_4PC ]                ) },
      { "tier10_2pc",                           OPT_BOOL,     &( set_bonus.count[ SET_T10_2PC ]               ) },
      { "tier10_4pc",                           OPT_BOOL,     &( set_bonus.count[ SET_T10_4PC ]               ) },
      { "meta_gem",                             OPT_STRING,   &( meta_gem_str                                 ) },
      // @option_doc loc=skip
      { "shoulder",                             OPT_STRING,   &( items[ SLOT_SHOULDERS ].options_str          ) },
      { "leg",                                  OPT_STRING,   &( items[ SLOT_LEGS      ].options_str          ) },
      { "foot",                                 OPT_STRING,   &( items[ SLOT_FEET      ].options_str          ) },
      { "wrist",                                OPT_STRING,   &( items[ SLOT_WRISTS    ].options_str          ) },
      { "hand",                                 OPT_STRING,   &( items[ SLOT_HANDS     ].options_str          ) },
      { "ring1",                                OPT_STRING,   &( items[ SLOT_FINGER_1  ].options_str          ) },
      { "ring2",                                OPT_STRING,   &( items[ SLOT_FINGER_2  ].options_str          ) },
      // @option_doc loc=player/all/gear title="Gear Stats"
      { "gear_strength",                        OPT_FLT,  &( gear.attribute[ ATTR_STRENGTH  ]                 ) },
      { "gear_agility",                         OPT_FLT,  &( gear.attribute[ ATTR_AGILITY   ]                 ) },
      { "gear_stamina",                         OPT_FLT,  &( gear.attribute[ ATTR_STAMINA   ]                 ) },
      { "gear_intellect",                       OPT_FLT,  &( gear.attribute[ ATTR_INTELLECT ]                 ) },
      { "gear_spirit",                          OPT_FLT,  &( gear.attribute[ ATTR_SPIRIT    ]                 ) },
      { "gear_spell_power",                     OPT_FLT,  &( gear.spell_power                                 ) },
      { "gear_mp5",                             OPT_FLT,  &( gear.mp5                                         ) },
      { "gear_attack_power",                    OPT_FLT,  &( gear.attack_power                                ) },
      { "gear_expertise_rating",                OPT_FLT,  &( gear.expertise_rating                            ) },
      { "gear_armor_penetration_rating",        OPT_FLT,  &( gear.armor_penetration_rating                    ) },
      { "gear_haste_rating",                    OPT_FLT,  &( gear.haste_rating                                ) },
      { "gear_hit_rating",                      OPT_FLT,  &( gear.hit_rating                                  ) },
      { "gear_crit_rating",                     OPT_FLT,  &( gear.crit_rating                                 ) },
      { "gear_health",                          OPT_FLT,  &( gear.resource[ RESOURCE_HEALTH ]                 ) },
      { "gear_mana",                            OPT_FLT,  &( gear.resource[ RESOURCE_MANA   ]                 ) },
      { "gear_rage",                            OPT_FLT,  &( gear.resource[ RESOURCE_RAGE   ]                 ) },
      { "gear_energy",                          OPT_FLT,  &( gear.resource[ RESOURCE_ENERGY ]                 ) },
      { "gear_focus",                           OPT_FLT,  &( gear.resource[ RESOURCE_FOCUS  ]                 ) },
      { "gear_runic",                           OPT_FLT,  &( gear.resource[ RESOURCE_RUNIC  ]                 ) },
      { "gear_armor",                           OPT_FLT,  &( gear.armor                                       ) },
      // @option_doc loc=player/all/enchant/stats title="Stat Enchants"
      { "enchant_strength",                     OPT_FLT,  &( enchant.attribute[ ATTR_STRENGTH  ]              ) },
      { "enchant_agility",                      OPT_FLT,  &( enchant.attribute[ ATTR_AGILITY   ]              ) },
      { "enchant_stamina",                      OPT_FLT,  &( enchant.attribute[ ATTR_STAMINA   ]              ) },
      { "enchant_intellect",                    OPT_FLT,  &( enchant.attribute[ ATTR_INTELLECT ]              ) },
      { "enchant_spirit",                       OPT_FLT,  &( enchant.attribute[ ATTR_SPIRIT    ]              ) },
      { "enchant_spell_power",                  OPT_FLT,  &( enchant.spell_power                              ) },
      { "enchant_mp5",                          OPT_FLT,  &( enchant.mp5                                      ) },
      { "enchant_attack_power",                 OPT_FLT,  &( enchant.attack_power                             ) },
      { "enchant_expertise_rating",             OPT_FLT,  &( enchant.expertise_rating                         ) },
      { "enchant_armor_penetration_rating",     OPT_FLT,  &( enchant.armor_penetration_rating                 ) },
      { "enchant_armor",                        OPT_FLT,  &( enchant.armor                                    ) },
      { "enchant_haste_rating",                 OPT_FLT,  &( enchant.haste_rating                             ) },
      { "enchant_hit_rating",                   OPT_FLT,  &( enchant.hit_rating                               ) },
      { "enchant_crit_rating",                  OPT_FLT,  &( enchant.crit_rating                              ) },
      { "enchant_health",                       OPT_FLT,  &( enchant.resource[ RESOURCE_HEALTH ]              ) },
      { "enchant_mana",                         OPT_FLT,  &( enchant.resource[ RESOURCE_MANA   ]              ) },
      { "enchant_rage",                         OPT_FLT,  &( enchant.resource[ RESOURCE_RAGE   ]              ) },
      { "enchant_energy",                       OPT_FLT,  &( enchant.resource[ RESOURCE_ENERGY ]              ) },
      { "enchant_focus",                        OPT_FLT,  &( enchant.resource[ RESOURCE_FOCUS  ]              ) },
      { "enchant_runic",                        OPT_FLT,  &( enchant.resource[ RESOURCE_RUNIC  ]              ) },
      // @option_doc loc=skip
      { "skip_actions",                         OPT_STRING, &( action_list_skip                               ) },
      { "elixirs",                              OPT_STRING, &( elixirs_str                                    ) },
      { "flask",                                OPT_STRING, &( flask_str                                      ) },
      { "food",                                 OPT_STRING, &( food_str                                       ) },
      { NULL, OPT_UNKNOWN, NULL }
    };

    option_t::copy( options, player_options );
  }

  return options;
}

// player_t::create =========================================================

player_t* player_t::create( sim_t*             sim,
                            const std::string& type,
                            const std::string& name,
                            int race_type )
{
  if ( type == "death_knight" )
  {
    return player_t::create_death_knight( sim, name, race_type );
  }
  else if ( type == "druid" )
  {
    return player_t::create_druid( sim, name, race_type );
  }
  else if ( type == "hunter" )
  {
    return player_t::create_hunter( sim, name, race_type );
  }
  else if ( type == "mage" )
  {
    return player_t::create_mage( sim, name, race_type );
  }
  else if ( type == "priest" )
  {
    return player_t::create_priest( sim, name, race_type );
  }
  else if ( type == "paladin" )
  {
    return player_t::create_paladin( sim, name, race_type );
  }
  else if ( type == "rogue" )
  {
    return player_t::create_rogue( sim, name, race_type );
  }
  else if ( type == "shaman" )
  {
    return player_t::create_shaman( sim, name, race_type );
  }
  else if ( type == "warlock" )
  {
    return player_t::create_warlock( sim, name, race_type );
  }
  else if ( type == "warrior" )
  {
    return player_t::create_warrior( sim, name, race_type );
  }
  else if ( type == "pet" )
  {
    return sim -> active_player -> create_pet( name );
  }
  return 0;
}


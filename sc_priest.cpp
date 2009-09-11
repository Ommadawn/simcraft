// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simcraft.h"

// ==========================================================================
// Priest
// ==========================================================================

struct priest_t : public player_t
{
  action_t* active_devouring_plague;
  action_t* active_holy_fire;
  action_t* active_shadow_word_pain;
  action_t* active_vampiric_touch;
  action_t* active_vampiric_embrace;

  // Buffs

  buff_t* buffs_devious_mind;
  buff_t* buffs_glyph_of_shadow;
  buff_t* buffs_improved_spirit_tap;
  buff_t* buffs_inner_fire;
  buff_t* buffs_inner_fire_armor;
  buff_t* buffs_shadow_form;
  buff_t* buffs_shadow_weaving;
  buff_t* buffs_surge_of_light;

  // Cooldowns
  struct _cooldowns_t
  {
    double mind_blast;
    double shadow_fiend;

    void reset() { memset( ( void* ) this, 0x00, sizeof( _cooldowns_t ) ); }
    _cooldowns_t() { reset(); }
  };
  _cooldowns_t _cooldowns;

  // Gains
  gain_t* gains_devious_mind;
  gain_t* gains_dispersion;
  gain_t* gains_glyph_of_shadow;
  gain_t* gains_improved_spirit_tap;
  gain_t* gains_shadow_fiend;
  gain_t* gains_surge_of_light;

  // Up-Times

  // Random Number Generators
  rng_t* rng_pain_and_suffering;

  // Options
  std::string power_infusion_target_str;

  // Mana Resource Tracker
  struct _mana_resource_t
  {
    double mana_gain;
    double mana_loss;

    void reset() { memset( ( void* ) this, 0x00, sizeof( _mana_resource_t ) ); }
    _mana_resource_t() { reset(); }
  };
  _mana_resource_t _mana_resource;

  double max_mana_cost;

  std::vector<player_t *> party_list;

  struct talents_t
  {
    int  aspiration;
    int  darkness;
    int  dispersion;
    int  divine_fury;
    int  enlightenment;
    int  focused_mind;
    int  focused_power;
    int  focused_will;
    int  holy_specialization;
    int  improved_devouring_plague;
    int  improved_inner_fire;
    int  improved_mind_blast;
    int  improved_power_word_fortitude;
    int  improved_shadow_word_pain;
    int  improved_spirit_tap;
    int  improved_vampiric_embrace;
    int  inner_focus;
    int  meditation;
    int  mental_agility;
    int  mental_strength;
    int  mind_flay;
    int  mind_melt;
    int  misery;
    int  pain_and_suffering;
    int  penance;
    int  power_infusion;
    int  searing_light;
    int  shadow_affinity;
    int  shadow_focus;
    int  shadow_form;
    int  shadow_power;
    int  shadow_weaving;
    int  spirit_of_redemption;
    int  spiritual_guidance;
    int  surge_of_light;
    int  twin_disciplines;
    int  twisted_faith;
    int  vampiric_embrace;
    int  vampiric_touch;
    int  veiled_shadows;

    talents_t() { memset( ( void* ) this, 0x0, sizeof( talents_t ) ); }
  };
  talents_t talents;

  struct glyphs_t
  {
    int dispersion;
    int hymn_of_hope;
    int inner_fire;
    int penance;
    int shadow;
    int shadow_word_death;
    int shadow_word_pain;
    int smite;
    glyphs_t() { memset( ( void* ) this, 0x0, sizeof( glyphs_t ) ); }
  };
  glyphs_t glyphs;

  bool   use_shadow_word_death;

  priest_t( sim_t* sim, const std::string& name, int race_type = RACE_NONE ) : player_t( sim, PRIEST, name, race_type )
  {
    // Active
    active_devouring_plague  = 0;
    active_holy_fire         = 0;
    active_shadow_word_pain  = 0;
    active_vampiric_touch    = 0;
    active_vampiric_embrace  = 0;

    use_shadow_word_death = false;

    max_mana_cost = 0.0;
  }

  // Character Definition
  virtual void      init_glyphs();
  virtual void      init_race();
  virtual void      init_base();
  virtual void      init_gains();
  virtual void      init_uptimes();
  virtual void      init_rng();
  virtual void      init_buffs();
  virtual void      init_actions();
  virtual void      reset();
  virtual void      init_party();
  virtual bool      get_talent_trees( std::vector<int*>& discipline, std::vector<int*>& holy, std::vector<int*>& shadow );
  virtual std::vector<option_t>& get_options();
  virtual bool      save( FILE* file, int save_type=SAVE_ALL );
  virtual action_t* create_action( const std::string& name, const std::string& options );
  virtual pet_t*    create_pet   ( const std::string& name );
  virtual int       decode_set( item_t& item );
  virtual int       primary_resource() SC_CONST { return RESOURCE_MANA; }
  virtual int       primary_role() SC_CONST     { return ROLE_SPELL; }
  virtual int       primary_tree() SC_CONST     { return talents.shadow_form ? TREE_SHADOW : talents.penance ? TREE_DISCIPLINE : TREE_HOLY; }
  virtual double    composite_armor() SC_CONST;
  virtual double    composite_attribute_multiplier( int attr ) SC_CONST;
  virtual double    composite_spell_power( int school ) SC_CONST;

  virtual void      regen( double periodicity );

  virtual double    resource_gain( int resource, double amount, gain_t* source=0, action_t* action=0 );
  virtual double    resource_loss( int resource, double amount, action_t* action=0 );
};

namespace   // ANONYMOUS NAMESPACE ==========================================
{

// ==========================================================================
// Priest Spell
// ==========================================================================

struct priest_spell_t : public spell_t
{
  priest_spell_t( const char* n, player_t* player, int s, int t ) :
      spell_t( n, player, RESOURCE_MANA, s, t )
  {

  }

  virtual double haste() SC_CONST;
  virtual void   execute();
  virtual void   player_buff();
  virtual void   assess_damage( double amount, int dmg_type );
};

// ==========================================================================
// Pet Shadow Fiend
// ==========================================================================

struct shadow_fiend_pet_t : public pet_t
{
  struct melee_t : public attack_t
  {
    melee_t( player_t* player ) :
        attack_t( "melee", player, RESOURCE_NONE, SCHOOL_SHADOW )
    {
      weapon = &( player -> main_hand_weapon );
      base_execute_time = weapon -> swing_time;
      weapon_multiplier = 0;
      direct_power_mod = 0.379;
      base_spell_power_multiplier = 1.0;
      base_attack_power_multiplier = 0.0;
      base_dd_multiplier = 1.15; // Shadowcrawl
      base_dd_min = 177;
      base_dd_max = 209;
      background = true;
      repeating  = true;
      may_dodge  = false;
      may_miss   = false;
      may_parry  = false;
      may_crit   = true;
      may_block  = true;
    }
    void assess_damage( double amount, int dmg_type )
    {
      attack_t::assess_damage( amount, dmg_type );
      priest_t* p = player -> cast_pet() -> owner -> cast_priest();
      p -> resource_gain( RESOURCE_MANA, p -> resource_max[ RESOURCE_MANA ] * 0.05, p -> gains_shadow_fiend );
    }
  };

  melee_t* melee;

  shadow_fiend_pet_t( sim_t* sim, player_t* owner ) :
      pet_t( sim, owner, "shadow_fiend" ), melee( 0 )
  {
    main_hand_weapon.type       = WEAPON_BEAST;
    main_hand_weapon.damage     = 100;
    main_hand_weapon.swing_time = 1.5;
    main_hand_weapon.school     = SCHOOL_SHADOW;

    stamina_per_owner = 0.51;
    intellect_per_owner = 0.30;
  }
  virtual void init_base()
  {
    pet_t::init_base();

    attribute_base[ ATTR_STRENGTH  ] = 153;
    attribute_base[ ATTR_AGILITY   ] = 108;
    attribute_base[ ATTR_STAMINA   ] = 280;
    attribute_base[ ATTR_INTELLECT ] = 133;

    base_attack_power = -20;
    initial_attack_power_per_strength = 2.0;

    melee = new melee_t( this );
  }
  virtual double composite_spell_power( int school ) SC_CONST
  {
    priest_t* p = owner -> cast_priest();

    double sp = p -> composite_spell_power( school );
    sp -= owner -> spirit() *   p -> spell_power_per_spirit;
    sp -= owner -> spirit() * ( p -> buffs_glyph_of_shadow -> check() ? 0.1 : 0.0 );
    return sp;
  }
  virtual void schedule_ready( double delta_time=0,
                               bool   waiting=false )
  {
    pet_t::schedule_ready( delta_time, waiting );
    if ( ! melee -> execute_event ) melee -> execute();
  }
  virtual void interrupt()
  {
    pet_t::interrupt();
    melee -> cancel();
  }
};

// trigger_misery ==============================================

static void trigger_misery( action_t* a )
{
  priest_t* p = a -> player -> cast_priest();
  target_t* t = p -> sim -> target;

  if( t -> debuffs.improved_faerie_fire -> check() >= p -> talents.misery )
    return;

  t -> debuffs.misery -> trigger( 1, p -> talents.misery, p -> talents.misery );
}

// ==========================================================================
// Priest Spell
// ==========================================================================

// priest_spell_t::haste ====================================================

double priest_spell_t::haste() SC_CONST
{
  priest_t* p = player -> cast_priest();

  double h = spell_t::haste();
  if ( p -> talents.enlightenment )
  {
    h *= 1.0 / ( 1.0 + p -> talents.enlightenment * 0.01 );
  }
  return h;
}

// priest_spell_t::execute ==================================================

void priest_spell_t::execute()
{
  priest_t* p = player -> cast_priest();

  spell_t::execute();

  if ( cost() > p -> max_mana_cost )
  {
    p -> max_mana_cost = cost();
  }

  if ( result_is_hit() )
  {
    if ( school == SCHOOL_SHADOW )
    {
      p -> buffs_shadow_weaving -> trigger();
    }
    if ( result == RESULT_CRIT )
    {
      p -> buffs_surge_of_light  -> trigger( 1, 1.0, p -> talents.surge_of_light * 0.25 );
    }
  }
}

// priest_spell_t::player_buff ==============================================

void priest_spell_t::player_buff()
{
  priest_t* p = player -> cast_priest();

  spell_t::player_buff();

  if ( school == SCHOOL_SHADOW )
  {
    player_multiplier *= 1.0 + p -> buffs_shadow_form -> check() * 0.15;
    player_multiplier *= 1.0 + p -> buffs_shadow_weaving -> stack() * 0.02;
  }

  if ( p -> talents.focused_power )
  {
    player_multiplier *= 1.0 + p -> talents.focused_power * 0.02;
  }
}

// priest_spell_t::assess_damage =============================================

void priest_spell_t::assess_damage( double amount,
                                    int    dmg_type )
{
  priest_t* p = player -> cast_priest();

  spell_t::assess_damage( amount, dmg_type );
  
  if ( p -> active_vampiric_embrace )
  {
    p -> resource_gain( RESOURCE_HEALTH, amount * 0.15 * ( 1.0 + p -> talents.improved_vampiric_embrace * 0.333333 ), p -> gains.vampiric_embrace );

    pet_t* r = p -> pet_list;

    while ( r )
    {
      r -> resource_gain( RESOURCE_HEALTH, amount * 0.03 * ( 1.0 + p -> talents.improved_vampiric_embrace * 0.333333 ), r -> gains.vampiric_embrace );
      r = r -> next_pet;
    }

    int num_players = p -> party_list.size();

    for ( int i=0; i < num_players; i++ )
    {
      player_t* q = p -> party_list[ i ];
      
      q -> resource_gain( RESOURCE_HEALTH, amount * 0.03 * ( 1.0 + p -> talents.improved_vampiric_embrace * 0.333333 ), q -> gains.vampiric_embrace );
    
      r = q -> pet_list;

      while ( r )
      {
        r -> resource_gain( RESOURCE_HEALTH, amount * 0.03 * ( 1.0 + p -> talents.improved_vampiric_embrace * 0.333333 ), r -> gains.vampiric_embrace );
        r = r -> next_pet;
      }
    }    
  }
}

// Holy Fire Spell ===========================================================

struct holy_fire_t : public priest_spell_t
{
  holy_fire_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "holy_fire", player, SCHOOL_HOLY, TREE_HOLY )
  {
    priest_t* p = player -> cast_priest();

    option_t options[] =
    {
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    static rank_t ranks[] =
    {
      { 80, 12, 900, 1140, 50, 0.11 }
      , // Dummy rank for level 80.
      { 78, 11, 890, 1130, 50, 0.11 },
      { 72, 10, 732,  928, 47, 0.11 },
      { 66,  9, 412,  523, 33, 0.11 },
      { 60,  8, 355,  449, 29, 0.13 },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    base_execute_time = 2.0;
    base_tick_time    = 1.0;
    num_ticks         = 7;
    cooldown          = 10;
    direct_power_mod  = 0.5715;
    tick_power_mod    = 0.1678 / 7;

    may_crit           = true;
    base_execute_time -= p -> talents.divine_fury * 0.01;
    base_multiplier   *= 1.0 + p -> talents.searing_light * 0.05;
    base_crit         += p -> talents.holy_specialization * 0.01;

    observer          = &( p -> active_holy_fire );
  }
};

// Smite Spell ================================================================

struct smite_t : public priest_spell_t
{
  smite_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "smite", player, SCHOOL_HOLY, TREE_HOLY )
  {
    priest_t* p = player -> cast_priest();

    option_t options[] =
    {
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    static rank_t ranks[] =
    {
      { 80, 13, 713, 799, 0, 0.15 }
      , // Dummy rank for level 80
      { 79, 12, 707, 793, 0, 0.15 },
      { 75, 11, 604, 676, 0, 0.15 },
      { 69, 10, 545, 611, 0, 0.15 },
      { 61,  9, 405, 455, 0, 0.17 },
      { 54,  8, 371, 415, 0, 0.17 },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    base_execute_time = 2.5;
    direct_power_mod  = base_execute_time / 3.5;
    may_crit          = true;

    base_execute_time -= p -> talents.divine_fury * 0.1;
    base_multiplier   *= 1.0 + p -> talents.searing_light * 0.05;
    base_crit         += p -> talents.holy_specialization * 0.01;
  }

  virtual void execute()
  {
    priest_spell_t::execute();
    player -> cast_priest() -> buffs_surge_of_light -> expire();
  }
  virtual double execute_time() SC_CONST
  {
    priest_t* p = player -> cast_priest();
    return p -> buffs_surge_of_light -> up() ? 0 : priest_spell_t::execute_time();
  }

  virtual double cost() SC_CONST
  {
    priest_t* p = player -> cast_priest();
    return p -> buffs_surge_of_light -> check() ? 0 : priest_spell_t::cost();
  }

  virtual void player_buff()
  {
    priest_spell_t::player_buff();
    priest_t* p = player -> cast_priest();
    may_crit = ! ( p -> buffs_surge_of_light -> check() );
    if ( p -> active_holy_fire && p -> glyphs.smite ) player_multiplier *= 1.20;
  }
};

// Penance Spell ===============================================================

struct penance_tick_t : public priest_spell_t
{
  penance_tick_t( player_t* player ) :
      priest_spell_t( "penance", player, SCHOOL_HOLY, TREE_HOLY )
  {
    priest_t* p = player -> cast_priest();

    static rank_t ranks[] =
    {
      { 80, 4, 288, 288, 0, 0 },
      { 76, 3, 256, 256, 0, 0 },
      { 68, 2, 224, 224, 0, 0 },
      { 60, 1, 184, 184, 0, 0 },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    dual       = true;
    background = true;
    may_crit   = true;

    direct_power_mod  = 0.8 / 3.5;

    base_multiplier *= 1.0 + p -> talents.searing_light * 0.05 + p -> talents.twin_disciplines * 0.01;
    base_crit       += p -> talents.holy_specialization * 0.01;
  }

  virtual void execute()
  {
    priest_spell_t::execute();
    tick_dmg = direct_dmg;
    update_stats( DMG_OVER_TIME );
  }
};

struct penance_t : public priest_spell_t
{
  spell_t* penance_tick;

  penance_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "penance", player, SCHOOL_HOLY, TREE_HOLY )
  {
    priest_t* p = player -> cast_priest();
    check_talent( p -> talents.penance );

    option_t options[] =
    {
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    may_miss = may_resist = false;

    base_cost         = 0.16 * p -> resource_base[ RESOURCE_MANA ];
    base_execute_time = 0.0;
    channeled         = true;
    tick_zero         = true;
    num_ticks         = 2;
    base_tick_time    = 1.0;

    cooldown  = 12 - ( p -> glyphs.penance * 2 );
    cooldown *= 1.0 - p -> talents.aspiration * 0.10;

    penance_tick = new penance_tick_t( p );
  }

  virtual void tick()
  {
    if ( sim -> debug ) log_t::output( sim, "%s ticks (%d of %d)", name(), current_tick, num_ticks );
    penance_tick -> execute();
  }
};

// Shadow Word Pain Spell ======================================================

struct shadow_word_pain_t : public priest_spell_t
{
  int shadow_weaving_wait;

  shadow_word_pain_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "shadow_word_pain", player, SCHOOL_SHADOW, TREE_SHADOW ), shadow_weaving_wait( 0 )
  {
    priest_t* p = player -> cast_priest();

    option_t options[] =
    {
      { "shadow_weaving_wait", OPT_BOOL, &shadow_weaving_wait },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    static rank_t ranks[] =
    {
      { 80, 12, 0, 0, 230, 0.22 },
      { 75, 11, 0, 0, 196, 0.22 },
      { 70, 10, 0, 0, 186, 0.22  },
      { 65,  9, 0, 0, 151, 0.25  },
      { 58,  8, 0, 0, 128, 0.25  },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    base_execute_time = 0;
    base_tick_time    = 3.0;
    num_ticks         = 6;
    tick_power_mod    = base_tick_time / 15.0;
    tick_power_mod   *= 0.915;  // Nerf Bat! Determined to be 0.915 after more rigorous testing.
    base_cost        *= 1.0 - ( util_t::talent_rank( p -> talents.mental_agility, 3, 0.04, 0.07, 0.10 ) +
                                p -> talents.shadow_focus    * 0.02 );

    base_multiplier *= 1.0 + ( p -> talents.darkness                  * 0.02 +
                               p -> talents.twin_disciplines          * 0.01 +
                               p -> talents.improved_shadow_word_pain * 0.03 );
    base_hit  += p -> talents.shadow_focus * 0.01;
    base_crit += p -> talents.mind_melt * 0.03;

    if ( p -> set_bonus.tier6_2pc_caster() ) num_ticks++;

    observer = &( p -> active_shadow_word_pain );
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();
    tick_may_crit = p -> buffs_shadow_form -> check() != 0;
    base_crit_bonus_multiplier = 1.0 + p -> buffs_shadow_form -> check();
    priest_spell_t::execute();
    if ( result_is_hit() )
    {
      trigger_misery( this );
    }
  }

  virtual bool ready()
  {
    priest_t* p = player -> cast_priest();

    if ( shadow_weaving_wait )
      if ( p -> talents.shadow_weaving )
	if ( p -> buffs_shadow_weaving -> check() < 5 )
	  return false;

    return priest_spell_t::ready();
  }
};

// Vampiric Touch Spell ======================================================

struct vampiric_touch_t : public priest_spell_t
{
  vampiric_touch_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "vampiric_touch", player, SCHOOL_SHADOW, TREE_SHADOW )
  {
    priest_t* p = player -> cast_priest();

    check_talent( p -> talents.vampiric_touch );

    option_t options[] =
    {
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    static rank_t ranks[] =
    {
      { 80, 5, 0, 0, 170, 0.16 },
      { 75, 4, 0, 0, 147, 0.16 },
      { 70, 3, 0, 0, 130, 0.16  },
      { 60, 2, 0, 0, 120, 0.18  },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    base_execute_time = 1.5;
    base_tick_time    = 3.0;
    num_ticks         = 5;
    tick_power_mod    = base_tick_time / 15.0;
    tick_power_mod   *= 2.0;

    base_cost       *= 1.0 - p -> talents.shadow_focus * 0.02;
    base_cost        = floor( base_cost );
    base_multiplier *= 1.0 + p -> talents.darkness * 0.02;
    base_hit        += p -> talents.shadow_focus * 0.01;
    base_crit       += p -> talents.mind_melt * 0.03;

    if ( p -> set_bonus.tier9_2pc_caster() ) num_ticks += 2;

    observer = &( p -> active_vampiric_touch );
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();
    tick_may_crit = p -> buffs_shadow_form -> check() != 0;
    base_crit_bonus_multiplier = 1.0 + p -> buffs_shadow_form -> check();
    priest_spell_t::execute();
    if ( result_is_hit() )
    {
      trigger_misery( this );
    }
  }
};

// Devouring Plague Spell ======================================================

struct devouring_plague_burst_t : public priest_spell_t
{
  devouring_plague_burst_t( player_t* player ) :
      priest_spell_t( "devouring_plague", player, SCHOOL_SHADOW, TREE_SHADOW )
  {
    priest_t* p = player -> cast_priest();

    static rank_t ranks[] =
    {
      { 79, 9, 172, 172, 0, 0 },
      { 73, 8, 143, 143, 0, 0 },
      { 68, 7, 136, 136, 0, 0 },
      { 60, 6, 113, 113, 0, 0 },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    // burst = 5%-15% of total periodic damage from 8 ticks

    dual       = true;
    proc       = true;
    background = true;
    may_crit   = true;

    base_multiplier *= 8.0 * p -> talents.improved_devouring_plague * 0.05;

    direct_power_mod  = 3.0 / 15.0;
    direct_power_mod *= 0.925;

    base_multiplier  *= 1.0 + ( p -> talents.darkness                  * 0.02 +
                                p -> talents.twin_disciplines          * 0.01 +
                                p -> talents.improved_devouring_plague * 0.05 );

    base_hit += p -> talents.shadow_focus * 0.01;

    // This helps log file and decouples the sooth RNG from the ticks.
    name_str = "devouring_plague_burst";
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();

    priest_spell_t::execute();
    if ( result_is_hit() )
    {
      if ( result == RESULT_CRIT )
      {
        p -> buffs_glyph_of_shadow -> trigger();
      }
    }
    update_stats( DMG_DIRECT );
  }

  virtual void player_buff()
  {
    priest_t* p = player -> cast_priest();
    priest_spell_t::player_buff();
    player_spell_power -= p -> spirit() *   p -> spell_power_per_spirit;
    player_spell_power -= p -> spirit() * ( p -> buffs_glyph_of_shadow -> check() ? 0.1 : 0.0 );
  }

  virtual void target_debuff( int dmg_type )
  {
    priest_spell_t::target_debuff( dmg_type );
    if ( sim -> target -> debuffs.crypt_fever -> up() ) target_multiplier *= 1.30;
  }
};

struct devouring_plague_t : public priest_spell_t
{
  spell_t* devouring_plague_burst;

  devouring_plague_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "devouring_plague", player, SCHOOL_SHADOW, TREE_SHADOW ), devouring_plague_burst( 0 )
  {
    priest_t* p = player -> cast_priest();

    option_t options[] =
    {
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    static rank_t ranks[] =
    {
      { 79, 9, 0, 0, 172, 0.25 },
      { 73, 8, 0, 0, 143, 0.25 },
      { 68, 7, 0, 0, 136, 0.25 },
      { 60, 6, 0, 0, 113, 0.28 },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    base_execute_time = 0;
    base_tick_time    = 3.0;
    num_ticks         = 8;
    cooldown          = 0.0;
    binary            = true;
    tick_power_mod    = base_tick_time / 15.0;
    tick_power_mod   *= 0.925;
    base_cost        *= 1.0 - ( util_t::talent_rank( p -> talents.mental_agility, 3, 0.04, 0.07, 0.10 ) + p -> talents.shadow_focus * 0.02 );
    base_cost         = floor( base_cost );
    base_multiplier  *= 1.0 + ( p -> talents.darkness                  * 0.02 +
                                p -> talents.twin_disciplines          * 0.01 +
                                p -> talents.improved_devouring_plague * 0.05 +
                                p -> set_bonus.tier8_2pc_caster()      * 0.15 );
    base_hit         += p -> talents.shadow_focus * 0.01;
    base_crit        += p -> talents.mind_melt * 0.03;

    if ( p -> talents.improved_devouring_plague )
    {
      devouring_plague_burst = new devouring_plague_burst_t( p );
    }

    observer = &( p -> active_devouring_plague );
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();
    tick_may_crit = p -> buffs_shadow_form -> check() != 0;
    base_crit_bonus_multiplier = 1.0 + p -> buffs_shadow_form -> check();
    priest_spell_t::execute();
    if ( devouring_plague_burst ) devouring_plague_burst -> execute();
  }

  virtual void tick()
  {
    priest_spell_t::tick();
    player -> resource_gain( RESOURCE_HEALTH, tick_dmg * 0.15 );
  }

  virtual void target_debuff( int dmg_type )
  {
    priest_spell_t::target_debuff( dmg_type );
    if ( sim -> target -> debuffs.crypt_fever -> up() ) target_multiplier *= 1.30;
  }

  virtual void update_stats( int type )
  {
    if ( devouring_plague_burst && type == DMG_DIRECT ) return;
    priest_spell_t::update_stats( type );
  }
};

// Vampiric Embrace Spell ======================================================

struct vampiric_embrace_t : public priest_spell_t
{
  vampiric_embrace_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "vampiric_embrace", player, SCHOOL_SHADOW, TREE_SHADOW )
  {
    priest_t* p = player -> cast_priest();

    check_talent( p -> talents.vampiric_embrace );

    static rank_t ranks[] =
    {
      { 1, 1, 0, 0, 0, 0.00 },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    base_execute_time = 0;
    base_tick_time    = 300.0;
    num_ticks         = 1;
    cooldown          = 0;
    base_cost         = 0.0;
    base_cost         = floor( base_cost );
    base_multiplier   = 0;
    base_hit          = p -> talents.shadow_focus * 0.01;

    observer = &( p -> active_vampiric_embrace );
  }
};

// Mind Blast Spell ============================================================

struct mind_blast_t : public priest_spell_t
{
  mind_blast_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "mind_blast", player, SCHOOL_SHADOW, TREE_SHADOW )
  {
    priest_t* p = player -> cast_priest();

    option_t options[] =
    {
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    static rank_t ranks[] =
    {
      { 80, 14, 997, 1053, 0, 0.17 }
      , // Dummy rank for level 80 characters.
      { 79, 13, 992, 1048, 0, 0.17 },
      { 74, 12, 837,  883, 0, 0.17 },
      { 69, 11, 708,  748, 0, 0.17 },
      { 63, 10, 557,  587, 0, 0.19 },
      { 58,  9, 503,  531, 0, 0.19 },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    base_execute_time = 1.5;
    cooldown          = 8.0;
    may_crit          = true;
    direct_power_mod  = base_execute_time / 3.5;

    base_cost        *= 1.0 - ( p -> talents.focused_mind * 0.05 +
                                p -> talents.shadow_focus * 0.02 +
                                p -> set_bonus.tier7_2pc_caster() ? 0.1 : 0.0  );
    base_cost         = floor( base_cost );
    base_multiplier  *= 1.0 + p -> talents.darkness * 0.02;
    base_hit         += p -> talents.shadow_focus * 0.01;
    base_crit        += p -> talents.mind_melt * 0.02;
    cooldown         -= p -> talents.improved_mind_blast * 0.5;
    direct_power_mod *= 1.0 + p -> talents.misery * 0.05;

    base_crit_bonus_multiplier *= 1.0 + p -> talents.shadow_power * 0.20;

    if ( p -> set_bonus.tier6_4pc_caster() ) base_multiplier *= 1.10;
  }

  virtual void execute()
  {
    priest_spell_t::execute();
    priest_t* p = player -> cast_priest();
    if ( result_is_hit() )
    {
      p -> buffs_devious_mind -> trigger();
      if ( result == RESULT_CRIT )
      {
        p -> buffs_improved_spirit_tap -> trigger();
        p -> buffs_glyph_of_shadow -> trigger();
      }
      if ( p -> active_vampiric_touch )
      {
	      p -> trigger_replenishment();
      }
    }
    p -> _cooldowns.mind_blast = cooldown_ready;
  }

  virtual void player_buff()
  {
    priest_spell_t::player_buff();
    priest_t* p = player -> cast_priest();
    if ( p -> talents.twisted_faith )
    {
      if ( p -> active_shadow_word_pain ) player_multiplier *= 1.0 + p -> talents.twisted_faith * 0.02;
    }
  }
};

// Shadow Word Death Spell ======================================================

struct shadow_word_death_t : public priest_spell_t
{
  double mb_wait;
  int    mb_priority;
  int    devious_mind_filler;

  shadow_word_death_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "shadow_word_death", player, SCHOOL_SHADOW, TREE_SHADOW ), mb_wait( 0 ), mb_priority( 0 ), devious_mind_filler( 0 )
  {
    priest_t* p = player -> cast_priest();

    option_t options[] =
    {
      { "mb_wait",             OPT_FLT,  &mb_wait             },
      { "mb_priority",         OPT_BOOL, &mb_priority         },
      { "devious_mind_filler", OPT_BOOL, &devious_mind_filler },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    static rank_t ranks[] =
    {
      { 80, 4, 750, 870, 0, 0.12 },
      { 75, 3, 639, 741, 0, 0.12 },
      { 70, 2, 572, 664, 0, 0.12 },
      { 62, 1, 450, 522, 0, 0.14 },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    base_execute_time = 0;
    may_crit          = true;
    cooldown          = 12.0;
    direct_power_mod  = ( 1.5/3.5 );
    base_cost        *= 1.0 - ( util_t::talent_rank( p -> talents.mental_agility, 3, 0.04, 0.07, 0.10 ) +
                                p -> talents.shadow_focus * 0.02 );
    base_cost         = floor( base_cost );
    base_multiplier  *= 1.0 + p -> talents.darkness * 0.02 + p -> talents.twin_disciplines * 0.01;
    base_hit         += p -> talents.shadow_focus * 0.01;

    base_crit_bonus_multiplier *= 1.0 + p -> talents.shadow_power * 0.20;

    if ( p -> set_bonus.tier7_4pc_caster() ) base_crit += 0.1;
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();
    priest_spell_t::execute();
    if ( result_is_hit() )
    {
      p -> resource_loss( RESOURCE_HEALTH, direct_dmg * ( 1.0 - p -> talents.pain_and_suffering * 0.10 ) );
      if ( result == RESULT_CRIT )
      {
        p -> buffs_improved_spirit_tap -> trigger();
        p -> buffs_glyph_of_shadow -> trigger();
      }
    }
  }

  virtual void player_buff()
  {
    priest_t* p = player -> cast_priest();
    priest_spell_t::player_buff();
    if ( p -> glyphs.shadow_word_death )
    {
      if ( sim -> target -> health_percentage() < 35 )
      {
        player_multiplier *= 1.1;
      }
    }
  }

  virtual bool ready()
  {
    priest_t* p = player -> cast_priest();

    if ( ! priest_spell_t::ready() )
      return false;

    if ( mb_wait )
    {
      if ( ( p -> _cooldowns.mind_blast - sim -> current_time ) < mb_wait )
        return false;
    }

    if ( mb_priority )
    {
      if ( ( p -> _cooldowns.mind_blast - sim -> current_time ) > ( haste() * 1.5 + sim -> gcd_lag + mb_wait ) )
        return false;
    }

    if ( devious_mind_filler )
    {
      if ( p -> buffs_devious_mind -> check() )
        return false;
    }

    return true;
  }
};

// Mind Flay Spell ============================================================

struct mind_flay_tick_t : public priest_spell_t
{
  mind_flay_tick_t( player_t* player ) :
      priest_spell_t( "mind_flay", player, SCHOOL_SHADOW, TREE_SHADOW )
  {
    priest_t* p = player -> cast_priest();

    static rank_t ranks[] =
    {
      { 80, 9, 196, 196, 0, 0 },
      { 74, 8, 164, 164, 0, 0 },
      { 68, 7, 150, 150, 0, 0 },
      { 60, 6, 121, 121, 0, 0 },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    dual              = true;
    background        = true;
    may_crit          = true;
    direct_power_mod  = 1.0 / 3.5;
    direct_power_mod *= 0.9;  // Nerf Bat!
    direct_power_mod *= 1.0 + p -> talents.misery * 0.05;
    base_hit         += p -> talents.shadow_focus * 0.01;
    base_multiplier  *= 1.0 + ( p -> talents.darkness         * 0.02 +
                                p -> talents.twin_disciplines * 0.01 );
    base_crit        += p -> talents.mind_melt * 0.02;

    if ( p -> set_bonus.tier9_4pc_caster() ) base_crit += 0.05;

    base_crit_bonus_multiplier *= 1.0 + p -> talents.shadow_power * 0.20;
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();
    priest_spell_t::execute();
    tick_dmg = direct_dmg;
    update_stats( DMG_OVER_TIME );
    if ( result_is_hit() )
    {
      if ( p -> active_shadow_word_pain )
      {
        if ( p -> rng_pain_and_suffering -> roll( p -> talents.pain_and_suffering * ( 1.0/3.0 ) ) )
        {
          p -> active_shadow_word_pain -> refresh_duration();
        }
      }
      if ( result == RESULT_CRIT )
      {
        if ( p -> sim -> P322 )
          p -> buffs_improved_spirit_tap -> trigger( 1, -1.0, p -> talents.improved_spirit_tap ? 0.5 : 0.0 );
        p -> buffs_glyph_of_shadow -> trigger();
      }
    }
  }

  virtual void player_buff()
  {
    priest_t* p = player -> cast_priest();
    priest_spell_t::player_buff();
    if ( p -> talents.twisted_faith )
    {
      if ( p -> active_shadow_word_pain )
      {
        player_multiplier *= 1.0 + p -> talents.twisted_faith * 0.02 + ( p -> glyphs.shadow_word_pain ? 0.10 : 0.00 );
      }
    }
  }
};

struct mind_flay_t : public priest_spell_t
{
  spell_t* mind_flay_tick;

  double mb_wait;
  int    swp_refresh;
  int    devious_mind_priority;
  int    cut_for_mb;

  mind_flay_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "mind_flay", player, SCHOOL_SHADOW, TREE_SHADOW ), mb_wait( 0 ), swp_refresh( 0 ), devious_mind_priority( 0 ),
                                                                         cut_for_mb( 0 )
  {
    priest_t* p = player -> cast_priest();
    check_talent( p -> talents.mind_flay );

    option_t options[] =
    {
      { "devious_mind_priority", OPT_BOOL, &devious_mind_priority },
      { "cut_for_mb",            OPT_BOOL, &cut_for_mb            },
      { "mb_wait",               OPT_FLT,  &mb_wait               },
      { "swp_refresh",           OPT_BOOL, &swp_refresh           },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    channeled      = true;
    num_ticks      = 3;
    base_tick_time = 1.0;

    base_cost  = 0.09 * p -> resource_base[ RESOURCE_MANA ];
    base_cost *= 1.0 - ( p -> talents.focused_mind * 0.05 +
                         p -> talents.shadow_focus * 0.02 );
    base_cost  = floor( base_cost );
    base_hit  += p -> talents.shadow_focus * 0.01;

    mind_flay_tick = new mind_flay_tick_t( p );
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();

    if ( cut_for_mb )
    {
      if ( ( p -> _cooldowns.mind_blast - sim -> current_time ) <= ( 2 * base_tick_time * haste() ) )
      {
        num_ticks = 2;
      }
    }

    priest_spell_t::execute();
    if ( result_is_hit() )
    {
      trigger_misery( this );
    }
  }

  virtual void tick()
  {
    if ( sim -> debug ) log_t::output( sim, "%s ticks (%d of %d)", name(), current_tick, num_ticks );
    mind_flay_tick -> execute();
    update_time( DMG_OVER_TIME );
  }

  virtual bool ready()
  {
    priest_t* p = player -> cast_priest();

    if ( ! priest_spell_t::ready() )
      return false;

    // Optional check to only cast Mind Flay if there's 2 or less ticks left on SW:P
    // This allows a action+=/mind_flay,swp_refresh to be added as a higher priority to ensure that SW:P doesn't fall off
    // Won't be necessary as part of a standard rotation, but if there's movement or other delays in casting it would have
    // it's uses.
    if ( swp_refresh && ( p -> talents.pain_and_suffering > 0 ) )
    {
      if ( ! p -> active_shadow_word_pain )
        return false;

      if ( ( p -> active_shadow_word_pain -> num_ticks -
             p -> active_shadow_word_pain -> current_tick ) > 2 )
        return false;
    }

    // If this option is set (with a value in seconds), don't cast Mind Flay if Mind Blast 
    // is about to come off it's cooldown.
    if ( mb_wait )
    {
      if ( ( p -> _cooldowns.mind_blast - sim -> current_time ) < mb_wait )
        return false;
    }

    // If this option is set, don't cast Mind Flay if we don't have the Tier8 4pc bonus up (only if the character has tier8_4pc=1)
    if ( devious_mind_priority )
    {
      if ( p -> set_bonus.tier8_4pc_caster() && ! p -> buffs_devious_mind -> check() )
        return false;
    }

    return true;
  }
};

// Dispersion Spell ============================================================

struct dispersion_t : public priest_spell_t
{
  dispersion_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "dispersion", player, SCHOOL_SHADOW, TREE_SHADOW )
  {
    priest_t* p = player -> cast_priest();

    check_talent( p -> talents.dispersion );

    base_execute_time = 0.0;
    base_tick_time    = 1.0;
    num_ticks         = 6;
    channeled         = true;
    harmful           = false;
    base_cost         = 0;
    cooldown          = 120;

    if ( p -> glyphs.dispersion )
    {
      cooldown        -= 45;
    }
  }

  virtual void tick()
  {
    priest_t* p = player -> cast_priest();
    p -> resource_gain( RESOURCE_MANA, 0.06 * p -> resource_max[ RESOURCE_MANA ], p -> gains_dispersion );
    priest_spell_t::tick();
  }

  virtual bool ready()
  {
    if ( ! priest_spell_t::ready() )
      return false;

    priest_t* p = player -> cast_priest();

    double regen_rate = p -> _mana_resource.mana_gain / sim -> current_time;
    double consumption_rate = ( p -> _mana_resource.mana_loss / sim -> current_time ) - regen_rate;
    double shadow_fiend_regen = 0.50 * p -> resource_max[ RESOURCE_MANA ];
    double time_to_die = sim -> target -> time_to_die();

    if ( consumption_rate <= 0.00001 ) return false;

    double oom_time = p -> resource_current[ RESOURCE_MANA ] / consumption_rate;

    if ( oom_time >= time_to_die ) return false;

    if ( p -> _cooldowns.shadow_fiend <= sim -> current_time ) return false;

    // Don't cast if Shadowfiend is still up
    if ( ( p -> _cooldowns.shadow_fiend - sim -> current_time ) > 
         ( ( 300.0 - p -> talents.veiled_shadows * 60.0 ) - 15.0 ) ) return false;

    if ( oom_time >= ( p -> _cooldowns.shadow_fiend - sim -> current_time) )
    {
      double new_mana = p -> resource_current[ RESOURCE_MANA ] - consumption_rate * ( p -> _cooldowns.shadow_fiend - sim -> current_time ) + shadow_fiend_regen;

      if ( new_mana > p -> resource_max[ RESOURCE_MANA ] )
        new_mana = p -> resource_max[ RESOURCE_MANA ];
  
      double oom_time2 = new_mana / consumption_rate;
      if ( ( p -> _cooldowns.shadow_fiend - sim -> current_time + oom_time2 ) >= time_to_die ) return false;

      if ( consumption_rate < ( shadow_fiend_regen / ( 300.0 - p -> talents.veiled_shadows * 60.0 ) ) ) return false;

      double consumption_rate2 = consumption_rate - ( shadow_fiend_regen / ( 300.0 - p -> talents.veiled_shadows * 60.0 ) );
      double oom_time3 = new_mana / consumption_rate2;

      if ( ( p -> _cooldowns.shadow_fiend - sim -> current_time + oom_time3 ) >= time_to_die ) return false;
    } 

    double trigger = p -> resource_max[ RESOURCE_MANA ] - p -> max_mana_cost;

    if ( ( p -> resource_max[ RESOURCE_MANA ] - p -> resource_current[ RESOURCE_MANA] ) <
         trigger ) return false;

    return true;
  }
};

// Power Infusion Spell =====================================================

struct power_infusion_t : public priest_spell_t
{
  player_t* power_infusion_target;

  power_infusion_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "power_infusion", player, SCHOOL_HOLY, TREE_DISCIPLINE )
  {
    priest_t* p = player -> cast_priest();
    check_talent( p -> talents.power_infusion );

    std::string target_str = p -> power_infusion_target_str;
    option_t options[] =
    {
      { "target", OPT_STRING, &target_str },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    if ( target_str.empty() )
    {
      power_infusion_target = p;
    }
    else
    {
      power_infusion_target = sim -> find_player( target_str );

      assert ( power_infusion_target != 0 );
    }

    trigger_gcd = 0;
    cooldown = 120.0;
    cooldown *= 1.0 - p -> talents.aspiration * 0.10;
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();
    if ( sim -> log ) log_t::output( sim, "%s performs %s", p -> name(), name() );

    if ( sim -> log ) log_t::output( sim, "%s grants %s Power Infusion", p -> name(), power_infusion_target -> name() );

    power_infusion_target -> buffs.power_infusion -> trigger();

    consume_resource();
    update_ready();
  }

  virtual bool ready()
  {
    if ( ! priest_spell_t::ready() )
      return false;

    if ( power_infusion_target == 0 )
      return false;

    if ( power_infusion_target -> buffs.bloodlust -> check() )
      return false;

    if ( power_infusion_target -> buffs.power_infusion -> check() )
      return false;

    return true;
  }
};

// Inner Focus Spell =====================================================

struct inner_focus_t : public priest_spell_t
{
  action_t* free_action;

  inner_focus_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "inner_focus", player, SCHOOL_HOLY, TREE_DISCIPLINE )
  {
    priest_t* p = player -> cast_priest();
    check_talent( p -> talents.inner_focus );

    cooldown = 180.0;
    cooldown *= 1.0 - p -> talents.aspiration * 0.10;

    std::string spell_name    = options_str;
    std::string spell_options = "";

    std::string::size_type cut_pt = spell_name.find_first_of( "," );

    if ( cut_pt != spell_name.npos )
    {
      spell_options = spell_name.substr( cut_pt + 1 );
      spell_name    = spell_name.substr( 0, cut_pt );
    }

    free_action = p -> create_action( spell_name.c_str(), spell_options.c_str() );
    free_action -> base_cost = 0;
    free_action -> background = true;
    free_action -> base_crit += 0.25;
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();
    if ( sim -> log ) log_t::output( sim, "%s performs %s", p -> name(), name() );
    p -> aura_gain( "Inner Focus" );
    p -> last_foreground_action = free_action;
    free_action -> execute();
    p -> aura_loss( "Inner Focus" );
    update_ready();
  }

  virtual bool ready()
  {
    return( priest_spell_t::ready() && free_action -> ready() );
  }
};

// Divine Spirit Spell =====================================================

struct divine_spirit_t : public priest_spell_t
{
  double bonus;

  divine_spirit_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "divine_spirit", player, SCHOOL_HOLY, TREE_DISCIPLINE ), bonus( 0 )
  {
    trigger_gcd = 0;
    bonus = util_t::ability_rank( player -> level,  80.0,80,  50.0,70,  40.0,0 );

    id = 48073;
  }

  virtual void execute()
  {
    if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), name() );

    for ( player_t* p = sim -> player_list; p; p = p -> next )
    {
      if ( p -> ooc_buffs() )
      {
        p -> buffs.divine_spirit -> trigger( 1, bonus );
        p -> init_resources( true );
      }
    }
  }

  virtual bool ready()
  {
    return player -> buffs.divine_spirit -> current_value < bonus;
  }
};

// Fortitude Spell ========================================================

struct fortitude_t : public priest_spell_t
{
  double bonus;

  fortitude_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "fortitude", player, SCHOOL_HOLY, TREE_DISCIPLINE ), bonus( 0 )
  {
    priest_t* p = player -> cast_priest();

    trigger_gcd = 0;

    bonus = util_t::ability_rank( player -> level,  165.0,80,  79.0,70,  54.0,0 );

    bonus *= 1.0 + p -> talents.improved_power_word_fortitude * 0.15;
    id = 48161;
  }

  virtual void execute()
  {
    if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), name() );

    for ( player_t* p = sim -> player_list; p; p = p -> next )
    {
      if ( p -> ooc_buffs() )
      {
	      p -> buffs.fortitude -> trigger( 1, bonus );
	      p -> init_resources( true );
      }
    }
  }

  virtual bool ready()
  {
    return player -> buffs.fortitude -> current_value < bonus;
  }
};

// Inner Fire Spell ======================================================

struct inner_fire_t : public priest_spell_t
{
  double bonus_spell_power;
  double bonus_armor;

  inner_fire_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "inner_fire", player, SCHOOL_HOLY, TREE_DISCIPLINE ), bonus_spell_power( 0 ), bonus_armor( 0 )
  {
    priest_t* p = player -> cast_priest();

    trigger_gcd = 0;

    bonus_spell_power = util_t::ability_rank( player -> level,  120.0,77,  95.0,71,  0.0,0     );
    bonus_armor       = util_t::ability_rank( player -> level,  2440.0,77, 1800.0,71, 1580.0,0 );

    bonus_spell_power *= 1.0 + p -> talents.improved_inner_fire * 0.15;
    bonus_armor       *= 1.0 + p -> talents.improved_inner_fire * 0.15;
    bonus_armor       *= 1.0 + p -> glyphs.inner_fire * 0.5;

    id = 48168;
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();

    if ( sim -> log ) log_t::output( sim, "%s performs %s", p -> name(), name() );
    
    p -> buffs_inner_fire       -> start( 1, bonus_spell_power );
    p -> buffs_inner_fire_armor -> start( 1, bonus_armor       );
  }

  virtual bool ready()
  {
    priest_t* p = player -> cast_priest();
    return ! p -> buffs_inner_fire -> check() || ! p -> buffs_inner_fire_armor -> check();
  }
};

// Shadow Form Spell =======================================================

struct shadow_form_t : public priest_spell_t
{
  shadow_form_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "shadow_form", player, SCHOOL_SHADOW, TREE_SHADOW )
  {
    priest_t* p = player -> cast_priest();
    check_talent( p -> talents.shadow_form );
    trigger_gcd = 0;
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();
    if ( sim -> log ) log_t::output( sim, "%s performs %s", p -> name(), name() );
    p -> buffs_shadow_form -> trigger();
  }

  virtual bool ready()
  {
    priest_t* p = player -> cast_priest();
    return( ! p -> buffs_shadow_form -> check() );
  }
};

// Shadow Fiend Spell ========================================================

struct shadow_fiend_spell_t : public priest_spell_t
{
  int trigger;

  shadow_fiend_spell_t( player_t* player, const std::string& options_str ) :
      priest_spell_t( "shadow_fiend", player, SCHOOL_SHADOW, TREE_SHADOW ), trigger( 0 )
  {
    priest_t* p = player -> cast_priest();

    option_t options[] =
    {
      { "trigger", OPT_INT, &trigger },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    static rank_t ranks[] =
    {
      { 1, 1, 0, 0, 0, 0.00 },
      { 0, 0, 0, 0, 0, 0 }
    };
    init_rank( ranks );

    harmful    = false;
    cooldown   = 300.0;
    cooldown  -= 60.0 * p -> talents.veiled_shadows;
    base_cost *= 1.0 - ( util_t::talent_rank( p -> talents.mental_agility, 3, 0.04, 0.07, 0.10 ) +
                         p -> talents.shadow_focus * 0.02 );
    base_cost  = floor( base_cost );
  }

  virtual void execute()
  {
    priest_t* p = player -> cast_priest();
    consume_resource();
    update_ready();
    double duration = 15.1;
    p -> summon_pet( "shadow_fiend", duration );
    p -> _cooldowns.shadow_fiend = cooldown_ready;
  }

  virtual bool ready()
  {
    if ( ! priest_spell_t::ready() )
      return false;

    priest_t* p = player -> cast_priest();

    if ( sim -> infinite_resource [ RESOURCE_MANA ] )
      return true;

    if ( trigger > 0 && 
         ( ( p -> resource_max    [ RESOURCE_MANA ] -
             p -> resource_current[ RESOURCE_MANA ] ) >= trigger ) ) return true;

    if ( trigger > 0 ) return false;

    // If it's not the first Shadowfiend just activate it anyway
    if ( cooldown_ready > 0.0 ) return true;

    if ( sim -> current_time < 15.0 ) return false;

    double shadow_fiend_regen = 0.50 * p -> resource_max[ RESOURCE_MANA ];

    if ( ( p -> resource_max[ RESOURCE_MANA ] - p -> resource_current[ RESOURCE_MANA ] ) >=
         shadow_fiend_regen ) return true;

    double regen_rate = p -> _mana_resource.mana_gain / sim -> current_time;
    double consumption_rate = ( p -> _mana_resource.mana_loss / sim -> current_time ) -
                              regen_rate;


    if ( consumption_rate <= ( shadow_fiend_regen / cooldown ) ) return true;

    int max_fiends_available = (int)( ( sim -> max_time - sim -> current_time ) / cooldown ) + 1;
    double mana_required = ( sim -> max_time - sim -> current_time ) * consumption_rate;
    double min_fiends_needed = ( mana_required - p -> resource_current[ RESOURCE_MANA ] ) / shadow_fiend_regen;

    double temp_trigger = shadow_fiend_regen;

    if ( min_fiends_needed <= ( max_fiends_available - 1.0 ) ) return true;

    if ( (int) min_fiends_needed <=  max_fiends_available ) 
    {
      temp_trigger = shadow_fiend_regen * ( min_fiends_needed - (double) max_fiends_available );
      if ( temp_trigger > shadow_fiend_regen )
        temp_trigger = shadow_fiend_regen;
    }

    if ( ( p -> resource_max    [ RESOURCE_MANA ] -
           p -> resource_current[ RESOURCE_MANA ] ) >= temp_trigger ) return true;

    return false;
  }
};

} // ANONYMOUS NAMESPACE ====================================================

// ==========================================================================
// Priest Character Definition
// ==========================================================================

// priest_t::composite_armor =========================================

double priest_t::composite_armor() SC_CONST
{
  double a = player_t::composite_armor();

  a += buffs_inner_fire_armor -> value();

  return floor( a );
}

// priest_t::composite_attribute_multiplier ================================

double priest_t::composite_attribute_multiplier( int attr ) SC_CONST
{
  double m = player_t::composite_attribute_multiplier( attr );

  if ( ( attr == ATTR_SPIRIT ) && ( buffs_improved_spirit_tap -> check() ) )
    m *= 1.0 + talents.improved_spirit_tap * 0.05;

  return m;
}

// priest_t::composite_spell_power =========================================

double priest_t::composite_spell_power( int school ) SC_CONST
{
  double sp = player_t::composite_spell_power( school );

  sp += buffs_inner_fire -> value();

  if ( buffs_glyph_of_shadow -> up() )
  {
    sp += spirit() * 0.10;
  }

  return floor( sp );
}

// priest_t::create_action ===================================================

action_t* priest_t::create_action( const std::string& name,
                                   const std::string& options_str )
{
  if ( name == "devouring_plague"  ) return new devouring_plague_t  ( this, options_str );
  if ( name == "dispersion"        ) return new dispersion_t        ( this, options_str );
  if ( name == "divine_spirit"     ) return new divine_spirit_t     ( this, options_str );
  if ( name == "fortitude"         ) return new fortitude_t         ( this, options_str );
  if ( name == "holy_fire"         ) return new holy_fire_t         ( this, options_str );
  if ( name == "inner_fire"        ) return new inner_fire_t        ( this, options_str );
  if ( name == "inner_focus"       ) return new inner_focus_t       ( this, options_str );
  if ( name == "mind_blast"        ) return new mind_blast_t        ( this, options_str );
  if ( name == "mind_flay"         ) return new mind_flay_t         ( this, options_str );
  if ( name == "penance"           ) return new penance_t           ( this, options_str );
  if ( name == "power_infusion"    ) return new power_infusion_t    ( this, options_str );
  if ( name == "shadow_word_death" ) return new shadow_word_death_t ( this, options_str );
  if ( name == "shadow_word_pain"  ) return new shadow_word_pain_t  ( this, options_str );
  if ( name == "shadow_form"       ) return new shadow_form_t       ( this, options_str );
  if ( name == "smite"             ) return new smite_t             ( this, options_str );
  if ( name == "shadow_fiend"      ) return new shadow_fiend_spell_t( this, options_str );
  if ( name == "vampiric_embrace"  ) return new vampiric_embrace_t  ( this, options_str );
  if ( name == "vampiric_touch"    ) return new vampiric_touch_t    ( this, options_str );

  return player_t::create_action( name, options_str );
}

// priest_t::create_pet ======================================================

pet_t* priest_t::create_pet( const std::string& pet_name )
{
  pet_t* p = find_pet( pet_name );

  if ( p ) return p;

  if ( pet_name == "shadow_fiend" ) return new shadow_fiend_pet_t( sim, this );

  return 0;
}

// priest_t::init_glyphs =====================================================

void priest_t::init_glyphs()
{
  memset( ( void* ) &glyphs, 0x0, sizeof( glyphs_t ) );

  std::vector<std::string> glyph_names;
  int num_glyphs = util_t::string_split( glyph_names, glyphs_str, ",/" );

  for ( int i=0; i < num_glyphs; i++ )
  {
    std::string& n = glyph_names[ i ];

    if      ( n == "dispersion"        ) glyphs.dispersion = 1;
    else if ( n == "penance"           ) glyphs.penance = 1;
    else if ( n == "hymn_of_hope"      ) glyphs.hymn_of_hope = 1;
    else if ( n == "inner_fire"        ) glyphs.inner_fire = 1;
    else if ( n == "shadow"            ) glyphs.shadow = 1;
    else if ( n == "shadow_word_death" ) glyphs.shadow_word_death = 1;
    else if ( n == "shadow_word_pain"  ) glyphs.shadow_word_pain = 1;
    else if ( n == "smite"             ) glyphs.smite = 1;
    // Just to prevent warnings....
    else if ( n == "circle_of_healing" ) ;
    else if ( n == "dispel_magic"      ) ;
    else if ( n == "fading"            ) ;
    else if ( n == "flash_heal"        ) ;
    else if ( n == "fortitude"         ) ;
    else if ( n == "guardian_spirit"   ) ;
    else if ( n == "levitate"          ) ;
    else if ( n == "mind_flay"         ) ;
    else if ( n == "pain_suppression"  ) ;
    else if ( n == "power_word_shield" ) ;
    else if ( n == "prayer_of_healing" ) ;
    else if ( n == "psychic_scream"    ) ;
    else if ( n == "renew"             ) ;
    else if ( n == "shackle_undead"    ) ;
    else if ( n == "shadow_protection" ) ;
    else if ( n == "shadowfiend"       ) ;
    else if ( ! sim -> parent ) util_t::printf( "simcraft: Player %s has unrecognized glyph %s\n", name(), n.c_str() );
  }
}

// priest_t::init_race ======================================================

void priest_t::init_race()
{
  race = util_t::parse_race_type( race_str );
  switch ( race )
  {
  case RACE_HUMAN:
  case RACE_DWARF:
  case RACE_NIGHT_ELF:
  case RACE_DRAENEI:
  case RACE_UNDEAD:
  case RACE_TROLL:
  case RACE_BLOOD_ELF:
    break;
  default:
    race = RACE_NIGHT_ELF;
    race_str = util_t::race_type_string( race );
  }

  player_t::init_race();
}

// priest_t::init_base =======================================================

void priest_t::init_base()
{
  attribute_base[ ATTR_STRENGTH  ] = rating_t::get_attribute_base( level, PRIEST, race, BASE_STAT_STRENGTH );
  attribute_base[ ATTR_AGILITY   ] = rating_t::get_attribute_base( level, PRIEST, race, BASE_STAT_AGILITY );
  attribute_base[ ATTR_STAMINA   ] = rating_t::get_attribute_base( level, PRIEST, race, BASE_STAT_STAMINA );
  attribute_base[ ATTR_INTELLECT ] = rating_t::get_attribute_base( level, PRIEST, race, BASE_STAT_INTELLECT );
  attribute_base[ ATTR_SPIRIT    ] = rating_t::get_attribute_base( level, PRIEST, race, BASE_STAT_SPIRIT );
  resource_base[ RESOURCE_HEALTH ] = rating_t::get_attribute_base( level, PRIEST, race, BASE_STAT_HEALTH );
  resource_base[ RESOURCE_MANA   ] = rating_t::get_attribute_base( level, PRIEST, race, BASE_STAT_MANA );
  base_spell_crit                  = rating_t::get_attribute_base( level, PRIEST, race, BASE_STAT_SPELL_CRIT );
  base_attack_crit                 = rating_t::get_attribute_base( level, PRIEST, race, BASE_STAT_MELEE_CRIT );
  initial_spell_crit_per_intellect = rating_t::get_attribute_base( level, PRIEST, race, BASE_STAT_SPELL_CRIT_PER_INT );
  initial_attack_crit_per_agility  = rating_t::get_attribute_base( level, PRIEST, race, BASE_STAT_MELEE_CRIT_PER_AGI );

  attribute_multiplier_initial[ ATTR_STAMINA   ] *= 1.0 + talents.improved_power_word_fortitude * 0.02;
  attribute_multiplier_initial[ ATTR_SPIRIT    ] *= 1.0 + talents.enlightenment * 0.02;
  attribute_multiplier_initial[ ATTR_SPIRIT    ] *= 1.0 + talents.spirit_of_redemption * 0.05;
  attribute_multiplier_initial[ ATTR_INTELLECT ] *= 1.0 + talents.mental_strength * 0.03;

  initial_spell_power_per_spirit = ( talents.spiritual_guidance * 0.05 +
                                     talents.twisted_faith      * ( sim -> P322 ? 0.04 : 0.02 ) );

  base_attack_power = -10;

  initial_attack_power_per_strength = 1.0;

  health_per_stamina = 10;
  mana_per_intellect = 15;

  base_spell_crit += talents.focused_will * 0.01;
}

// priest_t::init_gains ======================================================

void priest_t::init_gains()
{
  player_t::init_gains();

  gains_devious_mind        = get_gain( "devious_mind" );
  gains_dispersion          = get_gain( "dispersion" );
  gains_glyph_of_shadow     = get_gain( "glyph_of_shadow" );
  gains_improved_spirit_tap = get_gain( "improved_spirit_tap" );
  gains_shadow_fiend        = get_gain( "shadow_fiend" );
  gains_surge_of_light      = get_gain( "surge_of_light" );
}

// priest_t::init_uptimes ====================================================

void priest_t::init_uptimes()
{
  player_t::init_uptimes();
}

// priest_t::init_rng ========================================================

void priest_t::init_rng()
{
  player_t::init_rng();

  rng_pain_and_suffering = get_rng( "pain_and_suffering" );
}

// priest_t::init_buffs ======================================================

void priest_t::init_buffs()
{
  player_t::init_buffs();

  // buff_t( sim, player, name, max_stack, duration, cooldown, proc_chance, quiet )
  buffs_glyph_of_shadow     = new buff_t( this, "glyph_of_shadow",     1, 10.0                                                   );
  buffs_improved_spirit_tap = new buff_t( this, "improved_spirit_tap", 1, 8.0,  0.0, talents.improved_spirit_tap > 0 ? 1.0 : 0.0 );
  buffs_inner_fire          = new buff_t( this, "inner_fire"                                                                     );
  buffs_inner_fire_armor    = new buff_t( this, "inner_fire_armor"                                                               );
  buffs_shadow_weaving      = new buff_t( this, "shadow_weaving",      5, 15.0, 0.0, talents.shadow_weaving / 3.0                );
  buffs_shadow_form         = new buff_t( this, "shadow_form",         1                                                         );
  buffs_surge_of_light      = new buff_t( this, "surge_of_light",      1, 10.0                                                   );

  // stat_buff_t( sim, player, name, stat, amount, max_stack, duration, cooldown, proc_chance, quiet )
  buffs_devious_mind = new stat_buff_t( this, "devious_mind", STAT_HASTE_RATING, 240, 1, 4.0,  0.0, set_bonus.tier8_4pc_caster() );
}

// priest_t::init_actions =====================================================

void priest_t::init_actions()
{
  if ( action_list_str.empty() )
  {
    action_list_str = "flask,type=frost_wyrm/food,type=tender_shoveltusk_steak/fortitude/divine_spirit/inner_fire";
    action_list_str += "/snapshot_stats";
    int num_items = items.size();
    for ( int i=0; i < num_items; i++ )
    {
      if ( items[ i ].use.active() )
      {
        action_list_str += "/use_item,name=";
        action_list_str += items[ i ].name();
      }
    }
    switch ( primary_tree() )
    {
    case TREE_SHADOW:
      if ( talents.shadow_form ) action_list_str += "/shadow_form";
      action_list_str += "/wild_magic_potion";
      action_list_str += "/shadow_fiend";
      action_list_str += "/shadow_word_pain,shadow_weaving_wait=1";
      if ( talents.vampiric_touch ) action_list_str += "/vampiric_touch";
      action_list_str += "/devouring_plague/mind_blast";
      if ( talents.vampiric_embrace ) action_list_str += "/vampiric_embrace";
      if ( use_shadow_word_death ) action_list_str += "/shadow_word_death,mb_wait=0,mb_priority=0";
      if ( race == RACE_TROLL ) action_list_str += "/berserking";
      if ( race == RACE_BLOOD_ELF ) action_list_str += "/arcane_torrent";
      action_list_str += talents.mind_flay ? "/mind_flay" : "/smite";
      action_list_str += "/shadow_word_death,moving=1"; // when moving
      if ( talents.dispersion ) action_list_str += "/dispersion";
      break;
    case TREE_DISCIPLINE:
      action_list_str += "/mana_potion/shadow_fiend,trigger=10000";
      if ( talents.inner_focus ) action_list_str += "/inner_focus,shadow_word_pain";
      action_list_str += "/shadow_word_pain";
      if ( talents.power_infusion ) action_list_str += "/power_infusion";
      action_list_str += "/holy_fire/mind_blast/";
      if ( talents.penance ) action_list_str += "/penance";
      if ( race == RACE_TROLL ) action_list_str += "/berserking";
      if ( race == RACE_BLOOD_ELF ) action_list_str += "/arcane_torrent";
      action_list_str += "/smite";
      action_list_str += "/shadow_word_death"; // when moving
      break;
    case TREE_HOLY:
    default:
      action_list_str += "/mana_potion/shadow_fiend,trigger=10000";
      if ( talents.inner_focus ) action_list_str += "/inner_focus,shadow_word_pain";
      action_list_str += "/shadow_word_pain";
      if ( talents.power_infusion ) action_list_str += "/power_infusion";
      action_list_str += "/holy_fire/mind_blast";
      if ( race == RACE_TROLL     ) action_list_str += "/berserking";
      if ( race == RACE_BLOOD_ELF ) action_list_str += "/arcane_torrent";
      action_list_str += "/smite";
      action_list_str += "/shadow_word_death"; // when moving
      break;
    }

    action_list_default = 1;
  }

  player_t::init_actions();
}

// priest_t::init_party ======================================================

void priest_t::init_party()
{
  party_list.clear();

  if ( party == 0 )
    return;

  player_t* p = sim -> player_list;
  while ( p )
  {
     if ( ( p != this ) && ( p -> party == party ) && ( ! p -> quiet ) && ( ! p -> is_pet() ) )
     {
       party_list.push_back( p );
     }
     p = p -> next;
  }
}

// priest_t::reset ===========================================================

void priest_t::reset()
{
  player_t::reset();

  // Active
  active_devouring_plague = 0;
  active_holy_fire        = 0;
  active_shadow_word_pain = 0;
  active_vampiric_touch   = 0;
  active_vampiric_embrace = 0;

  _cooldowns.reset();
  _mana_resource.reset();

  init_party();
}

// priest_t::regen  ==========================================================

void priest_t::regen( double periodicity )
{
  mana_regen_while_casting = util_t::talent_rank( talents.meditation, 3, 0.17, 0.33, 0.50 );

  if ( buffs_improved_spirit_tap -> check() )
  {
    mana_regen_while_casting += util_t::talent_rank( talents.improved_spirit_tap, 2, 0.17, 0.33 );
  }

  player_t::regen( periodicity );
}

// priest_t::resource_gain ===================================================

double priest_t::resource_gain( int       resource,
                                double    amount,
                                gain_t*   source,
                                action_t* action )
{
  double actual_amount = player_t::resource_gain( resource, amount, source, action );

  if ( resource == RESOURCE_MANA )
  {
    if ( source != gains_shadow_fiend &&
         source != gains_dispersion )
    {
      _mana_resource.mana_gain += actual_amount;
    }
  }

  return actual_amount;
}

// priest_t::resource_loss ===================================================

double priest_t::resource_loss( int       resource,
                                double    amount,
                                action_t* action )
{
  double actual_amount = player_t::resource_loss( resource, amount, action );

  if ( resource == RESOURCE_MANA )
  {
    _mana_resource.mana_loss += actual_amount;
  }

  return actual_amount;
}

// priest_t::get_talent_trees ===============================================

bool priest_t::get_talent_trees( std::vector<int*>& discipline,
                                 std::vector<int*>& holy,
                                 std::vector<int*>& shadow )
{
  talent_translation_t translation[][3] =
  {
    { {  1, 0, NULL                                       }, {  1, 0, NULL                              }, {  1, 0, NULL                                   } },
    { {  2, 5, &( talents.twin_disciplines )              }, {  2, 0, NULL                              }, {  2, 2, &( talents.improved_spirit_tap )       } },
    { {  3, 0, NULL                                       }, {  3, 5, &( talents.holy_specialization )  }, {  3, 5, &( talents.darkness )                  } },
    { {  4, 3, &( talents.improved_inner_fire )           }, {  4, 0, NULL                              }, {  4, 3, &( talents.shadow_affinity )           } },
    { {  5, 2, &( talents.improved_power_word_fortitude ) }, {  5, 5, &( talents.divine_fury )          }, {  5, 2, &( talents.improved_shadow_word_pain ) } },
    { {  6, 0, NULL                                       }, {  6, 0, NULL                              }, {  6, 3, &( talents.shadow_focus )              } },
    { {  7, 3, &( talents.meditation )                    }, {  7, 0, NULL                              }, {  7, 0, NULL                                   } },
    { {  8, 1, &( talents.inner_focus )                   }, {  8, 0, NULL                              }, {  8, 5, &( talents.improved_mind_blast )       } },
    { {  9, 0, NULL                                       }, {  9, 0, NULL                              }, {  9, 1, &( talents.mind_flay )                 } },
    { { 10, 0, NULL                                       }, { 10, 0, NULL                              }, { 10, 2, &( talents.veiled_shadows )            } },
    { { 11, 3, &( talents.mental_agility )                }, { 11, 2, &( talents.searing_light )        }, { 11, 0, NULL                                   } },
    { { 12, 0, NULL                                       }, { 12, 0, NULL                              }, { 12, 3, &( talents.shadow_weaving )            } },
    { { 13, 0, NULL                                       }, { 13, 1, &( talents.spirit_of_redemption ) }, { 13, 0, NULL                                   } },
    { { 14, 5, &( talents.mental_strength )               }, { 14, 5, &( talents.spiritual_guidance )   }, { 14, 1, &( talents.vampiric_embrace )          } },
    { { 15, 0, NULL                                       }, { 15, 2, &( talents.surge_of_light )       }, { 15, 2, &( talents.improved_vampiric_embrace ) } },
    { { 16, 2, &( talents.focused_power )                 }, { 16, 0, NULL                              }, { 16, 3, &( talents.focused_mind )              } },
    { { 17, 3, &( talents.enlightenment )                 }, { 17, 0, NULL                              }, { 17, 2, &( talents.mind_melt )                 } },
    { { 18, 3, &( talents.focused_will )                  }, { 18, 0, NULL                              }, { 18, 3, &( talents.improved_devouring_plague ) } },
    { { 19, 1, &( talents.power_infusion )                }, { 19, 0, NULL                              }, { 19, 1, &( talents.shadow_form )               } },
    { { 20, 0, NULL                                       }, { 20, 0, NULL                              }, { 20, 5, &( talents.shadow_power )              } },
    { { 21, 0, NULL                                       }, { 21, 0, NULL                              }, { 21, 0, NULL                                   } },
    { { 22, 0, NULL                                       }, { 22, 0, NULL                              }, { 22, 3, &( talents.misery )                    } },
    { { 23, 2, &( talents.aspiration )                    }, { 23, 0, NULL                              }, { 23, 0, NULL                                   } },
    { { 24, 0, NULL                                       }, { 24, 0, NULL                              }, { 24, 1, &( talents.vampiric_touch )            } },
    { { 25, 0, NULL                                       }, { 25, 0, NULL                              }, { 25, 3, &( talents.pain_and_suffering )        } },
    { { 26, 0, NULL                                       }, { 26, 0, NULL                              }, { 26, 5, &( talents.twisted_faith )             } },
    { { 27, 0, NULL                                       }, { 27, 0, NULL                              }, { 27, 1, &( talents.dispersion )                } },
    { { 28, 1, &( talents.penance )                       }, {  0, 0, NULL                              }, {  0, 0, NULL                                   } },
    { {  0, 0, NULL                                       }, {  0, 0, NULL                              }, {  0, 0, NULL                                   } }
  };

  return get_talent_translation( discipline, holy, shadow, translation );
}

// priest_t::get_options ===================================================

std::vector<option_t>& priest_t::get_options()
{
  if ( options.empty() )
  {
    player_t::get_options();

    option_t priest_options[] =
    {
      // @option_doc loc=player/priest/talents title="Talents"
      { "aspiration",                    OPT_INT,    &( talents.aspiration                    ) },
      { "darkness",                      OPT_INT,    &( talents.darkness                      ) },
      { "dispersion",                    OPT_INT,    &( talents.dispersion                    ) },
      { "divine_fury",                   OPT_INT,    &( talents.divine_fury                   ) },
      { "enlightenment",                 OPT_INT,    &( talents.enlightenment                 ) },
      { "focused_mind",                  OPT_INT,    &( talents.focused_mind                  ) },
      { "focused_power",                 OPT_INT,    &( talents.focused_power                 ) },
      { "focused_will",                  OPT_INT,    &( talents.focused_will                  ) },
      { "holy_specialization",           OPT_INT,    &( talents.holy_specialization           ) },
      { "improved_devouring_plague",     OPT_INT,    &( talents.improved_devouring_plague     ) },
      { "improved_inner_fire",           OPT_INT,    &( talents.improved_inner_fire           ) },
      { "improved_mind_blast",           OPT_INT,    &( talents.improved_mind_blast           ) },
      { "improved_power_word_fortitude", OPT_INT,    &( talents.improved_power_word_fortitude ) },
      { "improved_shadow_word_pain",     OPT_INT,    &( talents.improved_shadow_word_pain     ) },
      { "improved_spirit_tap",           OPT_INT,    &( talents.improved_spirit_tap           ) },
      { "improved_vampiric_embrace",     OPT_INT,    &( talents.improved_vampiric_embrace     ) },
      { "inner_focus",                   OPT_INT,    &( talents.inner_focus                   ) },
      { "meditation",                    OPT_INT,    &( talents.meditation                    ) },
      { "mental_agility",                OPT_INT,    &( talents.mental_agility                ) },
      { "mental_strength",               OPT_INT,    &( talents.mental_strength               ) },
      { "mind_flay",                     OPT_INT,    &( talents.mind_flay                     ) },
      { "mind_melt",                     OPT_INT,    &( talents.mind_melt                     ) },
      { "misery",                        OPT_INT,    &( talents.misery                        ) },
      { "pain_and_suffering",            OPT_INT,    &( talents.pain_and_suffering            ) },
      { "penance",                       OPT_INT,    &( talents.penance                       ) },
      { "power_infusion",                OPT_INT,    &( talents.power_infusion                ) },
      { "searing_light",                 OPT_INT,    &( talents.searing_light                 ) },
      { "shadow_affinity",               OPT_INT,    &( talents.shadow_affinity               ) },
      { "shadow_focus",                  OPT_INT,    &( talents.shadow_focus                  ) },
      { "shadow_form",                   OPT_INT,    &( talents.shadow_form                   ) },
      { "shadow_power",                  OPT_INT,    &( talents.shadow_power                  ) },
      { "shadow_weaving",                OPT_INT,    &( talents.shadow_weaving                ) },
      { "spirit_of_redemption",          OPT_INT,    &( talents.spirit_of_redemption          ) },
      { "spiritual_guidance",            OPT_INT,    &( talents.spiritual_guidance            ) },
      { "surge_of_light",                OPT_INT,    &( talents.surge_of_light                ) },
      { "twin_disciplines",              OPT_INT,    &( talents.twin_disciplines              ) },
      { "twisted_faith",                 OPT_INT,    &( talents.twisted_faith                 ) },
      { "vampiric_embrace",              OPT_INT,    &( talents.vampiric_embrace              ) },
      { "vampiric_touch",                OPT_INT,    &( talents.vampiric_touch                ) },
      { "veiled_shadows",                OPT_INT,    &( talents.veiled_shadows                ) },
      // @option_doc loc=player/priest/glyphs title="Glyphs"
      { "glyph_hymn_of_hope",            OPT_BOOL,   &( glyphs.hymn_of_hope                   ) },
      { "glyph_penance",                 OPT_BOOL,   &( glyphs.penance                        ) },
      { "glyph_shadow_word_death",       OPT_BOOL,   &( glyphs.shadow_word_death              ) },
      { "glyph_shadow_word_pain",        OPT_BOOL,   &( glyphs.shadow_word_pain               ) },
      { "glyph_shadow",                  OPT_BOOL,   &( glyphs.shadow                         ) },
      { "glyph_smite",                   OPT_BOOL,   &( glyphs.smite                          ) },
      // @option_doc loc=player/priest/misc title="Misc"
      { "use_shadow_word_death",         OPT_BOOL,   &( use_shadow_word_death                 ) },
      { "power_infusion_target",         OPT_STRING, &( power_infusion_target_str             ) },
      { NULL, OPT_UNKNOWN, NULL }
    };

    option_t::copy( options, priest_options );
  }

  return options;
}

// priest_t::save =============================================================

bool priest_t::save( FILE* file, int save_type )
{
  player_t::save( file, save_type );

  if ( save_type == SAVE_ALL )
  {
    if ( ! power_infusion_target_str.empty() ) util_t::fprintf( file, "power_infusion_target=%s\n", power_infusion_target_str.c_str() );
  }

  return true;
}

// priest_t::decode_set =====================================================

int priest_t::decode_set( item_t& item )
{
  if ( item.slot != SLOT_HEAD      &&
       item.slot != SLOT_SHOULDERS &&
       item.slot != SLOT_CHEST     &&
       item.slot != SLOT_HANDS     &&
       item.slot != SLOT_LEGS      )
  {
    return SET_NONE;
  }

  const char* s = item.name();

  bool is_caster = ( strstr( s, "circlet"   ) ||
		     strstr( s, "mantle"    ) ||
		     strstr( s, "raiments"  ) ||
		     strstr( s, "handwraps" ) ||
		     strstr( s, "pants"     ) );

  if ( strstr( s, "faith" ) )
  {
    if ( is_caster ) return SET_T7_CASTER;
  }
  if ( strstr( s, "sanctification" ) )
  {
    if ( is_caster ) return SET_T8_CASTER;
  }
  if ( strstr( s, "zabras" ) ||
       strstr( s, "velens" ) )
  {
    if ( is_caster ) return SET_T9_CASTER;
  }

  return SET_NONE;
}

// ==========================================================================
// PLAYER_T EXTENSIONS
// ==========================================================================

// player_t::create_priest  =================================================

player_t* player_t::create_priest( sim_t* sim, const std::string& name, int race_type )
{
  priest_t* p =  new priest_t( sim, name, race_type );

  new shadow_fiend_pet_t( sim, p );

  return p;
}

// player_t::priest_init =====================================================

void player_t::priest_init( sim_t* sim )
{
  for ( player_t* p = sim -> player_list; p; p = p -> next )
  {
    p -> buffs.divine_spirit  = new stat_buff_t( p, "divine_spirit",   STAT_SPIRIT,   80.0, 1 );
    p -> buffs.fortitude      = new stat_buff_t( p, "fortitude",       STAT_STAMINA, 165.0, 1 );
    p -> buffs.power_infusion = new      buff_t( p, "power_infusion",             1,  15.0, 0 );
  }

  target_t* t = sim -> target;
  t -> debuffs.misery        = new debuff_t( sim, "misery", 1, 24.0 );
}

// player_t::priest_combat_begin =============================================

void player_t::priest_combat_begin( sim_t* sim )
{
  for ( player_t* p = sim -> player_list; p; p = p -> next )
  {
    if ( p -> ooc_buffs() )
    {
      if ( sim -> overrides.fortitude     ) p -> buffs.fortitude     -> override( 1, 165.0 * 1.30 );
      if ( sim -> overrides.divine_spirit ) p -> buffs.divine_spirit -> override( 1, 80.0 );
    }
  }

  target_t* t = sim -> target;
  if ( sim -> overrides.misery ) t -> debuffs.misery -> override( 1, 3 );
}

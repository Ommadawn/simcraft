<?php 
// === SUPPORT FUNCTIONS ===
/**
 * Handle implicit class-file loading
 * @param string $class_name
 * @return null
 */ 
function __autoload( $class_name ) 
{
	require_once "$class_name.inc.php";
}

/**
 * Return a full path corresponding with the input path, and throw an exception if the path doesn't exist
 * @param string $path
 * @return string
 */
function get_valid_path( $path )
{
	// If the path supplied isn't legit, throw an exception
	if( ! realpath($path) ) {
		throw new Exception("The supplied path ($path) does not have a valid real path.");
	}
	
	// Return the real path, with a single directory separator on the right
	return rtrim(realpath($path), '/') . '/';
}

/**
 * Simple helper function to speed up the laborious xpath process with SimpleXMLElement
 * @param $xml
 * @param $str_xpath
 * @return unknown_type
 */
function fetch_single_XML_xpath($xml, $str_xpath)
{
	$match = $xml->xpath($str_xpath);
	if( count($match) !== 1) {
		throw new Exception("More than one result was matched on this XML element");
	}
	return $match[0];
}
 
/**
 * Another simple helper function.  This one sets a single property on the fetched element
 * @return unknown_type
 */
function set_single_xml_xpath_property($xml, $str_xpath, $attribute_name, $attribute_value)
{
	$target = fetch_single_XML_xpath($xml, $str_xpath);
	$target[$attribute_name] = $attribute_value;
} 


// === BUILDING THE SIMCRAFT COMMAND ===

/**
 * Generate the simcraft system command, from the given array of options
 * @param array$arr_options
 * @return string
 */
function generate_simcraft_command( array $arr_options, $output_file=null )
{
	// Start with the simcraft invocation
	$simcraft_command = './simcraft';

	// Add the options array
	$simcraft_command .= recursive_build_option_string($arr_options);		
	
	// Add the output directives
	if( !is_null($output_file) ) {
		$simcraft_command .= " html='$output_file'";
	}
	
	// Return the assembled command
	return $simcraft_command;
}

/**
 * Recursively construct the options string
 * 
 * The array passed into this function is ultimately derived from the HTML form's fields
 * @param $array
 * @return string
 */
function recursive_build_option_string( array $array )
{
	// Init this array's string
	$str_return = '';
	
	// Loop over the elements of this array
	foreach($array as $index => $element) {

		// If the element is an array, recurse for deeper options 
		if( is_array($element) ) {
			$str_return .= recursive_build_option_string($element);
		}
		
		// Else, just append this element
		else {
			// If the element name is class, this is a special attribute that we inserted manually, handle it separately
			if( $index === 'class' ) {
				$str_return .= ' ' . $element . "='" . $array['name']."'";
			}
			
			// Since class and name are handled together, if this element is name, skip it
			else if( $index === 'name' ) {
			}
			
			// Otherwise, if the element isn't empty, append it to the string
			else if( $element !== '' ) {
				$str_return .= ' ' . $index . "='" . $element . "'";
			}
		}
	}
	
	// Return the created string
	return $str_return;
} 

// === BUILDING THE CONFIGURATION FORM ===

/**
 * Add the xml representing the configuration screen to the passed XML object
 * @param SimpleXMLElement $xml the object to which to attach the simulation configuration
 * @return null
 */
function append_simulation_config_form(SimpleXMLElement $xml)
{ 
	// Allow access to the global definition arrays (from defines.inc.php)
	global $ARR_SUPPORTED_CLASSES, $ARR_PLAYER_RELATED_SOURCE_FILES;

	
	// Build the array of C++ source files that constitute simulationcraft
	$arr_source_files = scandir(SIMULATIONCRAFT_PATH);
	foreach($arr_source_files as $index=>$directory_element) {
		if( pathinfo($directory_element, PATHINFO_EXTENSION)!=='cpp' ) {
			unset($arr_source_files[$index]);
		}
	}
	
	
	// Add the options from the individual character-class files
	$xml_player_options = $xml->addChild('supported_classes');
	foreach($ARR_SUPPORTED_CLASSES as $class_name => $class_array ) {
		
		// Remove this class's file from the array of source file names - it's already been handled
		unset($arr_source_files[array_search($class_array['source_file'], $arr_source_files)]);
		
		// Add the class element, with a class and label attribute
		$new_class = $xml_player_options->addChild('class');
		$new_class->addAttribute('class', $class_name);
		$new_class->addAttribute('label', ucwords(strtolower(str_replace('_', ' ', $class_name))));

		// Fetch the options for this class type
		if( !empty($class_array['source_file']) ) {
			$arr_options = parse_source_file_for_options( get_valid_path(SIMULATIONCRAFT_PATH) . $class_array['source_file'] );
			add_options_to_XML( $arr_options, $new_class);
		}		
	}

	
	// Add the options for all-classes
	$xml_all_class_options = $xml_player_options->addChild('class');
	$xml_all_class_options->addAttribute('class', 'all_classes');
	$xml_all_class_options->addAttribute('label', ucwords(strtolower(str_replace('_', ' ', 'all_classes'))));
	foreach($ARR_PLAYER_RELATED_SOURCE_FILES as $file_name ) {
		
		// Remove this class's file from the array of source file names - it's already been handled
		unset($arr_source_files[array_search($file_name, $arr_source_files)]);

		// Fetch the options for this class type
		if( !empty($file_name) ) {
			$arr_options = parse_source_file_for_options( get_valid_path(SIMULATIONCRAFT_PATH) . $file_name );
			add_options_to_XML( $arr_options, $xml_all_class_options);
		}		
	}
		
	
	// Add the global simulation options (the options in the files that weren't handled above)
	$xml_global_options = $xml->addChild('global_options');
	foreach( $arr_source_files as $file_name ) {		
		
		// Fetch the options for this class type
		if( !empty($file_name) ) {
			$arr_options = parse_source_file_for_options( get_valid_path(SIMULATIONCRAFT_PATH) . $file_name );
			add_options_to_XML( $arr_options, $xml_global_options);
		}		
	}
	
	// Add Raiding party members
	add_raid_members($xml);
		
	// Add the additional hand-tweaked options
	add_hand_edited_options($xml);
	
	// Set default values
	set_default_values($xml);
}

/**
 * Add some manually-derived options to the outgoing XML
 * 
 * No matter how good the auto-parsing of the C++ source files is, there's still some 
 * hand-editing to do.  Hopefully these changes won't go stale very quickly.
 * 
 * A lot of these options were found in sc_option.cpp, starting around line 306, revision 2186
 * @return unknown_type
 */
function add_hand_edited_options( SimpleXMLElement $xml )
{
	// Reach in and get the global-options tag
	$global_options = $xml->global_options ? $xml->global_options : $xml->addChild('global_options');
	
	// Get the all-classes options tag
	$all_classes = fetch_single_XML_xpath($xml->supported_classes, "class[@class='all_classes']");

	// Add the talent meta-parameter
	add_options_to_XML(array(array(
				'name' => 'talents',
				'type' => 'OPT_STRING',
				'tag' => '',
				'file' => 'meta_options'
		)), 
		$all_classes);

	// Add the optimal-raid meta-parameter
	add_options_to_XML(array(array(
				'name' => 'optimal_raid',
				'type' => 'OPT_INT',
				'tag' => '',
				'file' => 'meta_options'
		)), 
		$global_options);

	// Add the patch meta-parameter
	add_options_to_XML(array(array(
				'name' => 'patch',
				'type' => 'OPT_STRING',
				'tag' => '',
				'file' => 'meta_options'
		)), 
		$global_options);
		
}

/**
 * Add the default raid memmbers to the outgoing XML
 * @param $xml
 * @return unknown_type
 */
function add_raid_members( SimpleXMLElement $xml )
{
	// Add the default raid content
	add_raider_to_XML($xml, array('class'=>'warlock') );
}

/**
 * Add A raider to the target XML object
 * 
 * @param $class_name
 * @param $xml
 * @return unknown_type
 */
function add_raider_to_XML(SimpleXMLElement $xml, array $arr_option_values )
{
	// globalize the supported-class define array 
	global $ARR_SUPPORTED_CLASSES;
	
	// Make sure the class name is an allowed class
	if( !in_array($arr_option_values['class'], array_keys($ARR_SUPPORTED_CLASSES)) ) {
		throw new Exception("The supplied class name ({$arr_option_values['class']}) is not an allowed class.");
	}
		
	// If the raid-content tag isn't already present in the target xml, add it
	$xml_raid_content = $xml->raid_content ? $xml->raid_content : $xml->addChild('raid_content');
	
	// Add the player tag
	$new_raider = $xml_raid_content->addChild('player'); 
	
	// Add any options this raider has by default
	foreach($arr_option_values as $option_name => $option_value) {
		$new_raider->addAttribute($option_name, $option_value);
	}
}

/**
 * Attempt to set some default values
 * @param $xml
 * @return unknown_type
 */
function set_default_values( SimpleXMLElement $xml )
{
	// Reach in and get the global-options tag
	$global_options = $xml->global_options ? $xml->global_options : $xml->addChild('global_options');
	
	// Get the all-classes options tag
	$all_classes = fetch_single_XML_xpath($xml->supported_classes, "class[@class='all_classes']");
	
	// Most of these were lifted from the Globals_T7 file
	set_single_xml_xpath_property($global_options, "option[@name='patch']", 'value', '3.1.0');
	set_single_xml_xpath_property($global_options, "option[@name='queue_lag']", 'value', 0.075);
	set_single_xml_xpath_property($global_options, "option[@name='gcd_lag']", 'value', 0.150);
	set_single_xml_xpath_property($global_options, "option[@name='channel_lag']", 'value', 0.250);
	set_single_xml_xpath_property($global_options, "option[@name='target_level']", 'value', 83);
	set_single_xml_xpath_property($global_options, "option[@name='max_time']", 'value', 300);
	set_single_xml_xpath_property($global_options, "option[@name='iterations']", 'value', 1000);
	set_single_xml_xpath_property($global_options, "option[@name='infinite_mana']", 'value', 0);
	set_single_xml_xpath_property($global_options, "option[@name='regen_periodicity']", 'value', 1.0);
	set_single_xml_xpath_property($global_options, "option[@name='target_armor']", 'value', 10643);
	set_single_xml_xpath_property($global_options, "option[@name='target_race']", 'value', none);
	set_single_xml_xpath_property($global_options, "option[@name='faerie_fire']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='mangle']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='battle_shout']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='sunder_armor']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='thunder_clap']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='blessing_of_kings']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='blessing_of_wisdom']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='blessing_of_might']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='judgement_of_wisdom']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='sanctified_retribution']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='swift_retribution']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='crypt_fever']", 'value', 1);
	set_single_xml_xpath_property($global_options, "option[@name='calculate_scale_factors']", 'value', 0);
	set_single_xml_xpath_property($global_options, "option[@name='optimal_raid']", 'value', 1);
}

/**
 * Parse one of the simulationcraft C++ source files for the command-line options it handles
 * 
 * this method relies heavily on regular expressions, and assumes the existence of a parse_options()
 * method in the file, which must contain an array of options.  If this source format changes,
 * this function will need to be rebuilt.
 * @param string $file_path
 * @return array
 */
function parse_source_file_for_options( $file_path ) 
{
	// Insist the file exists
	if( !is_file($file_path)) {
		throw new Exception("The given file path ($file_path) is not a file");
	}

	// Load the file in question's contents, stripping out any new-lines
	$file_contents = file_get_contents( $file_path );
	$file_contents = str_replace("\n", '', $file_contents);
	
	// Match for the options (fun with regular expressions)
	preg_match('/bool [-_A-Za-z0-9]+::parse_option\(.*options\[\][\s]*=[\s]*{(.*);/U', $file_contents, $parse_matches);
	$options = $parse_matches[1];
	preg_match_all('/{[\s]*"([^,]*)",[\s]*([^,]*),[\s]*&\([\s]*([^,.]*)[^,]*\)[\s]*}[\s]*,/', $options, $parse_matches, PREG_SET_ORDER);
	//var_dump($parse_matches);
	
	// Arrange the options in a sane array
	$arr_output = array();
	foreach($parse_matches as $array) {
		$arr_output[] = array(
				'name' => trim($array[1]), 
				'type' => trim($array[2]), 
				'tag' => trim($array[3]), 
				'file' => $file_path
			);
	}

	// Return the array of matches	
	return $arr_output;
}

/**
 * Given an array of options, add the options to the given xml element
 * 
 * The array of options should be formated as subarrays with a 'name' and 'type' attribute.
 * @param array $arr_options
 * @param SimpleXMLElement $xml_object
 * @return null
 */
function add_options_to_XML( array $arr_options, SimpleXMLElement $xml_object)
{
	// Loop over the supplied array of options
	foreach($arr_options as $arr_option ) {

		// Ignore talent tags, these will be passed as a 'talents' URL
		if( $arr_option['tag'] === 'talents' ) {
			continue;
		}
		
		
		// === Convert the C++ types to html field types ===
		// certain types of OPT_INT options can be assumed to be boolean checkboxes
		if( $arr_option['type']==='OPT_INT' && in_array($arr_option['tag'], array('glyphs', 'idols', 'totems', 'tiers')) ) {
			$html_type = 'boolean';
		}
		
		// All other OPT_INTs are just text fields
		else if( $arr_option['type']==='OPT_INT' ) {
			$html_type = 'text';
		}
		
		// strings are text fields
		else if( $arr_option['type']==='OPT_STRING' ) {
			$html_type = 'text';
		}
		
		// floats are text fields
		else if( $arr_option['type']==='OPT_FLT' ) {
			$html_type = 'text';
		}
		
		// Do not display options that are not of the above types
		else {
			continue;
		}

		
		// Add the option to the xml object given		
		$xml_new = $xml_object->addChild('option');
		$xml_new->addAttribute('name', $arr_option['name']);
		$xml_new->addAttribute('label', ucwords(strtolower(str_replace('_', ' ', $arr_option['name']))) );
		$xml_new->addAttribute('type', $html_type);
		$xml_new->addAttribute('cpp_type', $arr_option['type']);
		$xml_new->addAttribute('tag', $arr_option['tag']);
		$xml_new->addAttribute('file', basename($arr_option['file']) );
		$xml_new->addAttribute('value', '');
	}
}
?>
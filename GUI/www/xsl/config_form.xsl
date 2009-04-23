<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

	<xsl:output method="html" encoding="utf-8" doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd" doctype-public="-//W3C//DTD XHTML 1.0 Transitional//EN" cdata-section-elements="pre script style" indent="yes" />
	
	
	<!--  Top-level XML tag this matches the root of the xml, and is the top of the xsl parsing tree -->
	<xsl:template match="/xml">

	<html xmlns="http://www.w3.org/1999/xhtml">
		
		<head>
			<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
			<meta name="description" content="" />
			<meta name="keywords" content="" />
			
			<title>SimulationCraft</title>
			
			<link rel="stylesheet" type="text/css" href="css/reset.css" />
			<link rel="stylesheet" type="text/css" href="css/config_form.css" />
			
			<script type="text/javascript" src="http://jqueryjs.googlecode.com/files/jquery-1.2.6.min.js"></script>
			<script type="text/javascript" src="js/config_form.js"></script>
		</head>
				
		<body>
		
			<!-- Call the template defined below, which handles initializing the prototypical new-raiders in javascript -->
			<xsl:call-template name="javascript_prefill" />

			<!-- The overall page content -->		
			<div id="content_wrapper">

				<form id="config_form" name="config_form" action="simulation.xml" method="post" target="_blank">


					<!-- Raid Composition -->
					<fieldset id="raid_composition" class="page_panel">
						<legend class="page_panel">Raid Composition</legend>

						<!-- Sidebar (handles adding new members to the raid) -->	
						<div id="sidebar">
						
							<!--  Menu of classes, used to add new members to the raid -->
							<div id="new_member_wrapper">
								<h1>Add By Class</h1>
												
								<ul id="new_member">
								
									<!-- For each of hte classes defined in the XML file (Except 'all_classes'), add a list element -->
									<xsl:for-each select="supported_classes/class[not(@class='all_classes')]">
									<xsl:sort select="@class" />
									
										<li>
											<xsl:attribute name="class">supported_class <xsl:value-of select="@class" /></xsl:attribute>
											<xsl:attribute name="title"><xsl:value-of select="@label" /></xsl:attribute>
											<span><xsl:value-of select="@label" /></span>
										</li>
									
									</xsl:for-each>
								</ul>	
							</div>
	
							<!-- Instructional caption -->
							<span class="list_caption">Select one of the above classes to add a new member to the raid</span>
		
						</div>
										
										
						<!--  The main list of raid members (generated with a template defined below) -->
						<ul id="raid_members">
							<xsl:apply-templates select="raid_content/player" />
						</ul>
					</fieldset>	



					<!-- Global simulation settings -->
					<fieldset id="global_settings" class="page_panel">
						<legend class="page_panel">Global Settings</legend>

						<!-- Loop over each option in the global set, sorted by the file in which the option originated -->
						<xsl:for-each select="global_options/option">
							<xsl:sort select="@file" />
					
							<!-- If this source file has changed from the previous option in this list (or if this is the first iteration of this loop), call the template -->
							<xsl:if test="position()=1 or @file != preceding-sibling::option[1]/@file">
								<xsl:call-template name="global_options_section">
									<xsl:with-param name="file_name" select="@file" />
								</xsl:call-template>
							</xsl:if>
						</xsl:for-each>

					</fieldset>


					<!--  Form submit/reset buttons -->
					<input class="go_button" type="submit" value="Simulate" />		
					<input class="reset_button" type="reset" value="Reset" />

				</form>
			</div>

		</body>
	</html>
	</xsl:template>


	<!-- Generate a section of global options, given the file name as a parameter -->
	<xsl:template name="global_options_section">
	
		<!-- For which file name is this section being generated? -->
		<xsl:param name="file_name" />
		
		<fieldset class="foldable folded">
			<legend><xsl:value-of select="$file_name" /></legend>
			
			<ul class="user_options">
	
				<!-- Loop over the sibling options to this option, selecting only the options that have the given file name (sorted by label) -->
				<xsl:for-each select="../option[@file=$file_name]">
				<xsl:sort select="@label" />
					<li>
					
						<!-- Call the general template for options, with 'globals' as the generated field name -->
						<xsl:apply-templates select=".">
							<xsl:with-param name="variable_name">globals</xsl:with-param>
						</xsl:apply-templates>
					</li>
				</xsl:for-each>
			</ul>
		</fieldset>
	</xsl:template>
	
	
	<!-- Javascript array describing the classes -->
	<xsl:template name="javascript_prefill">
		<xsl:for-each select="supported_classes/class">

			<!-- Because javascript sucks and has no HEREDOC equivalent, we have to do this the hard way... -->
			<!-- each class will get a hidden div holding a prototype new-raider, with a replacable tag string in place of it's index number -->
			<!-- On add, javascript will copy this hidden div, and replace the tag string with the actual index number -->
			<div class="hidden">
				<xsl:attribute name="id">hidden_div_<xsl:value-of select="@class" /></xsl:attribute>
			
				<!-- Call the template that handles displaying a raider instance (same as the one used to print the visible raider list elements) -->
				<xsl:call-template name="raid_content_player">
					<xsl:with-param name="class" select="@class" />
					<xsl:with-param name="index" select="'{INDEX_VALUE}'" />
				</xsl:call-template>
			</div>
			
		</xsl:for-each>
	</xsl:template>
		
	
	<!-- players in the raid-content tag.  This template gets used to generate the visible elements as well as the prototypical hidden raiders, 
			which get used by javascript to insert new players -->
	<xsl:template match="raid_content/player" name="raid_content_player">
		
		<!-- What's the character-class for this new player? (defaults to the player tag's class attribute, if its present) -->
		<xsl:param name="class" select="@class" />
		
		<!-- What index should we use (default to the XML element's position index) -->
		<xsl:param name="index" select="position()" />

		<li class="raider">
		
			<!-- Close button -->
			<div class="close_button" title="Remove this Player"></div>						
		
			<!--  Class description block (with the text looked up in the supported_classes tag) -->
			<div>
				<xsl:attribute name="class">member_class <xsl:value-of select="$class" /></xsl:attribute>
				<xsl:value-of select="//supported_classes/class[@class=$class]/@label" />
			</div>
			
			<!-- Hidden option variable defining the class for the raider -->
			<xsl:call-template name="simcraft_option">
				<xsl:with-param name="type">hidden</xsl:with-param>
				<xsl:with-param name="variable_name">raider</xsl:with-param>
				<xsl:with-param name="array_index"><xsl:value-of select="$index" /></xsl:with-param>
				<xsl:with-param name="value"><xsl:value-of select="$class" /></xsl:with-param>
				<xsl:with-param name="field_name">class</xsl:with-param>
			</xsl:call-template>
			
			
			<!-- List of options for this user (except for class, which shows up above as a hidden) -->
			<!-- Show all the straight gear-stat related fields -->
			<xsl:call-template name="player_option_list">
				<xsl:with-param name="index" select="$index" />
				<xsl:with-param name="name" select="'Gear'" />
				<xsl:with-param name="which" select="//supported_classes/class[@class='all_classes']/option[not(@name='class') and @tag='gear'] | //supported_classes/class[@class=$class]/option[not(@name='class') and @tag='gear']" />
			</xsl:call-template>
			
			<!-- Show all the options dealing with tier gear effects -->
			<xsl:call-template name="player_option_list">
				<xsl:with-param name="index" select="$index" />
				<xsl:with-param name="name" select="'Tier Gear Effects'" />
				<xsl:with-param name="which" select="//supported_classes/class[@class='all_classes']/option[not(@name='class') and @tag='tiers'] | //supported_classes/class[@class=$class]/option[not(@name='class') and @tag='tiers']" />
			</xsl:call-template>

			<!-- Show the glyph options -->
			<xsl:call-template name="player_option_list">
				<xsl:with-param name="index" select="$index" />
				<xsl:with-param name="name" select="'Glyphs'" />
				<xsl:with-param name="which" select="//supported_classes/class[@class='all_classes']/option[not(@name='class') and @tag='glyphs'] | //supported_classes/class[@class=$class]/option[not(@name='class') and @tag='glyphs']" />
			</xsl:call-template>

			<!-- Show the Idol options -->
			<xsl:call-template name="player_option_list">
				<xsl:with-param name="index" select="$index" />
				<xsl:with-param name="name" select="'Idols'" />
				<xsl:with-param name="which" select="//supported_classes/class[@class='all_classes']/option[not(@name='class') and @tag='idols'] | //supported_classes/class[@class=$class]/option[not(@name='class') and @tag='idols']" />
			</xsl:call-template>

			<!-- Show the Totem options -->
			<xsl:call-template name="player_option_list">
				<xsl:with-param name="index" select="$index" />
				<xsl:with-param name="name" select="'Totems'" />
				<xsl:with-param name="which" select="//supported_classes/class[@class='all_classes']/option[not(@name='class') and @tag='totems'] | //supported_classes/class[@class=$class]/option[not(@name='class') and @tag='totems']" />
			</xsl:call-template>

			<!-- Show all the 'all-classes' and class specific fields that weren't shown above -->
			<xsl:call-template name="player_option_list">
				<xsl:with-param name="index" select="$index" />
				<xsl:with-param name="name" select="'Other'" />
				<xsl:with-param name="which" select="//supported_classes/class[@class='all_classes']/option[not(@name='class') and not(@tag='glyphs') and not(@tag='tiers') and not(@tag='gear') and not(@tag='idols') and not(@tag='totems')] | //supported_classes/class[@class=$class]/option[not(@name='class') and not(@tag='glyphs') and not(@tag='tiers') and not(@tag='gear') and not(@tag='idols') and not(@tag='totems')]" />
			</xsl:call-template>
			
		</li>
	</xsl:template>


	<!-- A group of options for a raid member -->
	<xsl:template name="player_option_list">
		
		<!-- What index should we use in the HTML field name (ensures that PHP gets an array of values) -->
		<xsl:param name="index" />
		
		<!-- What's the name of the field? -->
		<xsl:param name="name" />
		
		<!-- Which option elements should be selected to build the list? -->
		<xsl:param name="which" />
		
		<!-- If the which variable actually finds any elements, show the fieldset -->
		<xsl:if test="$which">
			<fieldset class="option_section foldable folded">
				<legend><xsl:value-of select="$name" /></legend>
			
				<ul class="raider_options user_options">
			
				<!-- For each of the options selected in the $which parameter, build a list-element for it -->
				<xsl:for-each select="$which">
					<xsl:sort select="@label" />
					<li>
					
						<!-- Mark checkbox list elements specially, for CSS hooking -->
						<xsl:if test="@type='boolean'">
							<xsl:attribute name="class">checkbox</xsl:attribute>
						</xsl:if>
										
						<!-- Call the template that handles options, with the appropriate parameters -->
						<xsl:apply-templates select=".">
							<xsl:with-param name="variable_name">raider</xsl:with-param>
							<xsl:with-param name="array_index"><xsl:value-of select="$index" /></xsl:with-param>
						</xsl:apply-templates>
					</li>
				</xsl:for-each>
				
				</ul>
			</fieldset>
		</xsl:if>
	</xsl:template>



	<!-- === OPTION FIELDS === -->
	<!-- Any of the option fields in the page, this template gets called in multiple places above -->
	<xsl:template match="option" name="simcraft_option">
	
		<!-- Optional type override, defaults to the option's type attribute -->
		<xsl:param name="type" select="@type" />

		<!-- Optional value override, defaults to the option's value attribute -->
		<xsl:param name="value" select="@value" />
		
		<!-- the variable name to use in the form submission -->
		<xsl:param name="variable_name" />
		
		<!--An alternate index attribute, to turn the variable into an array -->
		<xsl:param name="array_index" select="''"  />
	
		<!-- the name of the option parameter -->
		<xsl:param name="field_name" select="@name" />

		<!-- The label to show the user -->
		<xsl:param name="field_label" select="@label" />
	
	
		<!-- Generate the string to use for ID's -->
		<xsl:variable name="id_string"><xsl:value-of select="$variable_name" />_<xsl:if test="not($array_index='')"><xsl:value-of select="$array_index"/>_</xsl:if><xsl:value-of select="$field_name" /></xsl:variable>

		<!-- Generate the string to use for the field name -->
		<xsl:variable name="name_string"><xsl:value-of select="$variable_name" /><xsl:if test="not($array_index='')">[<xsl:value-of select="$array_index"/>]</xsl:if>[<xsl:value-of select="$field_name" />]</xsl:variable>

		<!-- Show the label for non-hidden fields -->
		<xsl:if test="not($type='hidden')">
			<label>
				<xsl:attribute name="for"><xsl:value-of select="$id_string" /></xsl:attribute>
				<xsl:value-of select="$field_label" />
			</label>
		</xsl:if>
		
		
		<!-- Build the input field -->
		<input>
		
			<!-- Set the field name and id attributes -->
			<xsl:attribute name="name"><xsl:value-of select="$name_string" /></xsl:attribute>
			<xsl:attribute name="id"><xsl:value-of select="$id_string" /></xsl:attribute>
		
			<!-- set the input-type from the xml type attribute -->	
			<xsl:attribute name="type">
				<xsl:choose>
					<xsl:when test="$type='integer'">text</xsl:when>
					<xsl:when test="$type='float'">text</xsl:when>
					<xsl:when test="$type='string'">text</xsl:when>
					<xsl:when test="$type='boolean'">checkbox</xsl:when>
					<xsl:when test="$type='hidden'">hidden</xsl:when>
					<xsl:otherwise></xsl:otherwise>
				</xsl:choose>
			</xsl:attribute>									
		
			<!-- For non-checkbox fields, set the default value from the value attribute -->
			<xsl:attribute name="value">
				<xsl:choose>
					<xsl:when test="$type='boolean'">1</xsl:when>
					<xsl:otherwise><xsl:value-of select="$value" /></xsl:otherwise>
				</xsl:choose>
			</xsl:attribute>

			<!--  for boolean types, set the checked value of the checkbox from the value -->		
			<xsl:if test="$type='boolean' and $value='true'">
				<xsl:attribute name="checked">checked</xsl:attribute>
			</xsl:if>
		
		</input>					
	</xsl:template> 

</xsl:stylesheet>
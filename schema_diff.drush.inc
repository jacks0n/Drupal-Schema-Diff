<?php
/**
 * @file
 * Command line utility support for the Schema Diff module.
 */

require_once __DIR__ . '/schema_diff.utils.inc';

/**
 * Implements hook_drush_command().
 *
 * @todo Add an update hook generator command.
 */
function schema_diff_drush_command() {
  $commands['diff-schema'] = [
    'description' => dt('Display differences between a schema spec (in code) and the DB (live) version.'),
    'aliases'     => ['ds'],
    'examples'    => [
      'drush diff-schema node' => dt('Display differences between the `node` schema definition and DB version.'),
    ],
    'arguments'   => [
      'schema_name' => dt('Name of schema to diff (e.g. `node`).'),
    ],
  ];

  return $commands;
}

/**
 * Implements hook_drush_help().
 */
function schema_diff_drush_help($section) {
  $commands = schema_diff_drush_command();
  $command = substr($section, 6);

  return _schema_diff_get_default($commands[$command]['description']);
}

/**
 * Implements drush_COMMANDFILE_COMMANDNAME().
 */
function drush_schema_diff_diff_schema($schema_name = '') {
  if (!$schema_name) {
    return drush_set_error(dt('No schema was specified.'));
  }

  try {
    $schema_diff = _schema_diff_diff_schema($schema_name);
  }
  catch (Exception $e) {
    $error_type = get_class($e);
    return drush_set_error($error_type, $error_type . ': ' . $e->getMessage());
  }

  if (!class_exists('Console_Table')) {
    return drush_set_error('error', 'Missing class `Console_Table`. Please install this first!');
  }

  if (!empty($schema_diff['extraneous_fields'])) {
    print "\n\nExtraneous Fields\n";
    $table = new Console_Table();
    $table->setHeaders(['Column']);
    foreach ($schema_diff['extraneous_fields'] as $extraneous_field) {
      $table->addRow([$extraneous_field]);
    }
    print $table->getTable();
  }

  if (!empty($schema_diff['missing_fields'])) {
    print "\n\nMissing Fields\n";
    $table = new Console_Table();
    $table->setHeaders(['Column', 'Spec']);
    foreach ($schema_diff['missing_fields'] as $field_name => $field_spec) {
      $table->addRow([
        $field_name,
        drush_var_export($field_spec),
      ]);
    }
    print $table->getTable();
  }

  if (!empty($schema_diff['different_fields'])) {
    print "\n\nDifferent Columns\n";
    $table = new Console_Table();
    $table->setHeaders(['Column', 'Attribute', 'Spec', 'Actual']);
    foreach ($schema_diff['different_fields'] as $field_name => $field_diffs) {
      foreach ($field_diffs as $diff_type => $field_diff) {
        $table->addRow([
          $field_name,
          $diff_type,
          drush_var_export($field_diff['spec']),
          drush_var_export($field_diff['db']),
        ]);
      }
    }
    print $table->getTable();
  }
}

<?php
/**
 * @file
 * Utility functions.
 */

require_once __DIR__ . '/SchemaNotFoundException.class.inc';

/**
 * Generates a diff between a schema spec (in code) and the DB (live) version.
 *
 * @param array[] $schema_name
 *   Schema table name.
 *
 * @return array[]
 *   List of missing, extraneous, and different (than the spec) fields.
 *   Missing table, missing and extraneous field(s), etc. TODO - fill.
 *
 * @see drupal_get_schema()
 */
function _schema_diff_diff_schema($schema_name) {
  // Get the defined (in code) schema spec.
  $drupal_schema = drupal_get_schema($schema_name, TRUE);
  if (!$drupal_schema) {
    throw new SchemaNotFoundException(t('Could not find schema `!schema_name`.', [
      '!schema_name' => $schema_name,
    ]));
  }

  // Check the table exists.
  $is_table_missing = !db_table_exists($schema_name);
  if ($is_table_missing) {
    throw new DatabaseSchemaObjectDoesNotExistException(t('Could not find table `!table_name`.', [
      '!table_name' => $schema_name,
    ]));
  }

  // Fetch the real (live) DB schema.
  try {
    $db_schema_query = db_query(
      'SHOW COLUMNS FROM ' . $schema_name
    );
    // Index by column name.
    $db_schema['fields'] = array_reduce($db_schema_query->fetchAll(), function($result, $db_field_schema) {
      $result[$db_field_schema->Field] = (array) $db_field_schema;
      return $result;
    }, []);
  }
  catch (PDOException $e) {
    throw new PDOException(t('Could not `SHOW COLUMNS` for table `!table_name`. Error: !error.', [
      '!table_name' => $schema_name,
      '!error'      => end($e->errorInfo),
    ]));
  }

  $schema_diff = [];

  // Add missing fields.
  foreach ($drupal_schema['fields'] as $field_name => $field_spec) {
    if (!db_field_exists($schema_name, $field_name)) {
      $schema_diff['missing_fields'][$field_name] = $field_spec;
    }
  }

  // Add extraneous fields.
  $extraneous_fields = array_values(array_diff(
    array_keys($db_schema['fields']),
    array_keys($drupal_schema['fields'])
  ));
  if ($extraneous_fields) {
    $schema_diff['extraneous_fields'] = $extraneous_fields;
  }

  // Add fields not matching the spec.
  foreach ($drupal_schema['fields'] as $field_name => $field_spec) {
    // Ignore fields that don't exist in the DB.
    if (empty($db_schema['fields'][$field_name])) {
      continue;
    }

    $field_schema_diff = _schema_diff_diff_field_schema($field_spec, $db_schema['fields'][$field_name]);
    if ($field_schema_diff) {
      $schema_diff['different_fields'][$field_name] = $field_schema_diff;
    }
  }

  return $schema_diff;
}

/**
 * Generates a diff between a field (in code) spec and the DB (live) version.
 *
 * Note that foreign keys are ignored. From the Schema API documentation:
 * "This key is for documentation purposes only; foreign keys are not created
 * in the database, nor are they enforced by Drupal."
 *
 * @param array[] $drupal_field_spec
 *   Defined (in code) field spec.
 * @param array[] $db_field_spec
 *   DB field (live) spec.
 *
 * @return array[]
 *   Associative array, where keys are attributes which do not match
 *   and values are arrays containing 'spec' and 'db fields containing
 *   the spec and DB attribute values.
 *
 * @see includes/database/schema.inc
 * @see https://api.drupal.org/api/drupal/includes!database!schema.inc/group/schemaapi/7.x
 *
 * @todo Add support for indexes.
 * @todo Add support for unique key(s).
 * @todo Add support for primary key(s).
 */
function _schema_diff_diff_field_schema(array $drupal_field_spec, array $db_field_spec) {
  $field_schema_diff = [];
  $field_name = $db_field_spec['Field'];

  // Diff the `NOT NULL` constraint.
  $is_field_spec_not_null = !empty($drupal_field_spec['not null']);
  $is_db_field_not_null = ($db_field_spec['Null'] === 'NO');
  if ($is_field_spec_not_null !== $is_db_field_not_null) {
    $field_schema_diff['not null'] = [
      'spec' => $is_field_spec_not_null,
      'db'   => $is_db_field_not_null,
    ];
  }

  // Diff the `UNSIGNED` attribute.
  // e.g. bool(true).
  $is_field_spec_unsigned = !empty($drupal_field_spec['unsigned']);
  // e.g. "int(100) unsigned".
  $is_db_field_unsigned = strpos($db_field_spec['Type'], 'unsigned') !== FALSE;
  if ($is_field_spec_unsigned !== $is_db_field_unsigned) {
    $field_schema_diff['unsigned'] = [
      'spec' => $is_field_spec_unsigned,
      'db'   => $is_db_field_unsigned,
    ];
  }

  // `DEFAULT` attribute doesn't match.
  if ($drupal_field_spec['default'] != $db_field_spec['Default']) {
    $field_schema_diff[$field_name]['default'] = [
      'spec' => $drupal_field_spec['default'],
      'db'   => $db_field_spec['Default'],
    ];
  }

  // Diff the `LENGTH` attribute.
  // e.g. "int(100)".
  if (isset($drupal_field_spec['type'])) {
    $drupal_field_spec_full_type = "${drupal_field_spec['type']}(${drupal_field_spec['length']})";
    if (isset($drupal_field_spec['length']) && $db_field_spec['Type'] !== $drupal_field_spec_full_type) {
      $field_schema_diff['length'] = [
        'spec' => $drupal_field_spec_full_type,
        'db'   => $db_field_spec['Type'],
      ];
    }
  }

  // Diff the `TYPE` attribute.
  $db_field_type_map = Database::getConnection()->schema()->getFieldTypeMap();
  // E.g. `int:medium`.
  $drupal_type_size = $drupal_field_spec['type'] . ':' . _schema_diff_get_default($drupal_field_spec['size'], 'normal');
  // Convert the Drupal type and size to database specific types.
  // E.g. 'int:medium' => 'MEDIUMINT' for MySQL, 'int' for PostgreSQL.
  $expected_db_field_type = strtoupper(_schema_diff_get_default($db_field_type_map[$drupal_type_size]));
  // E.g. 'int(10)' => 'INT'.
  $db_field_type = strtoupper(current(explode('(', $db_field_spec['Type'])));
  if ($expected_db_field_type !== $db_field_type) {
    $field_schema_diff['type'] = [
      'spec' => $expected_db_field_type,
      'db'   => $db_field_type,
    ];
  }

  return $field_schema_diff;
}

/**
 * Helper function to return the given variable if it's not empty.
 *
 * @param mixed $var
 *   Variable to get, which may or may not exist.
 * @param mixed $default
 *   Value to return if $var is empty.
 *
 * @return mixed
 *   $var if it is not empty, otherwise $default.
 */
function _schema_diff_get_default(&$var, $default = NULL) {
  return !empty($var) ? $var : $default;
}

<?php
  /**
   * @file commerce_sagepay_formatters.inc
   * Common functions for all payment methods to format the cart for SagePay.
   */

  /**
   * Convert a Commerce Order object to XML using the V3 protocol format.
   *
   * @param $order
   * @return \DOMDocument
   */
  function _commerce_sagepay_cart_to_xml($order) {

    $order_wrapper = entity_metadata_wrapper('commerce_order', $order);

    // Define a new XML document object to store the order data.
    $xml_doc = new DOMDocument();


    $root = $xml_doc->appendChild(
      $xml_doc->createElement("basket")
    );

    $root->appendChild(
      $xml_doc->createElement('agentId', 'Drupal Commerce')
    );

    $order_lines = $order->commerce_line_items['und'];

    // Set up a variable to count the total number of units in the order as we
    // look through the lines.
    $total_items = 0;

    // Loop through order lines.
    foreach ($order_lines as $line) {

      // Load the line object and wrap it for easier manipulation.
      $line_item = commerce_line_item_load($line['line_item_id']);
      $line_item_wrapper = entity_metadata_wrapper('commerce_line_item', $line_item);

      // Check if this is a delivery line - we'll need this later.
      $line_item_type = $line_item->type;
      if ($line_item_type == 'shipping') $delivery_line = $line_item_wrapper;

      // Create an XML element for each order line.
      $linexml = $root->appendChild(
        $xml_doc->createElement('item')
      );

      $linexml->appendChild(
        $xml_doc->createElement('description', $line_item->line_item_label)
      );

      $linexml->appendChild(
        $xml_doc->createElement('quantity', $line_item->quantity)
      );

      $total_items += $line_item->quantity;

      $item_value = $line_item->commerce_unit_price['und'][0]['data']['components'][0]['price']['amount'] / 100;

      // Step through components to look for tax item
      $item_tax = 0;
      foreach ($line_item->commerce_unit_price['und'][0]['data']['components'] as $c) {
        if (strpos($c['name'], 'tax') === 0) {
          $item_tax += $c['price']['amount'] / 100;
        }
      }
      $item_total = $line_item_wrapper->commerce_unit_price->amount->value() / 100;

      $arr_line_total = commerce_price_component_total($line_item_wrapper->commerce_total->value());
      $line_total = $arr_line_total['amount'] / 100;

      $linexml->appendChild(
        $xml_doc->createElement('unitNetAmount', $item_value)
      );

      $linexml->appendChild(
        $xml_doc->createElement('unitTaxAmount', $item_tax)
      );

      $linexml->appendChild(
        $xml_doc->createElement('unitGrossAmount', $item_total)
      );

      $linexml->appendChild(
        $xml_doc->createElement('totalGrossAmount', $line_total)
      );


      // Check if line has an attached shipping profile.
      // Otherwise use the details from the order shipping profile.
      if (isset($line_item_wrapper->commerce_customer_shipping)) {
        $shipping_profile = $line_item_wrapper->commerce_customer_shipping;
      }
      else if (isset($order_wrapper->commerce_customer_shipping)) {
        $shipping_profile = $order_wrapper->commerce_customer_shipping;
      }

      // If we have no shipping profile, fall back to the billing address.
      if (!isset($shipping_profile)) {
        $shipping_profile = $order_wrapper->commerce_customer_billing;
      }

      // If we still have no address, skip this section of the XML
      if (isset($shipping_profile)) {
        $address = $shipping_profile->commerce_customer_address->value();
        $linexml->appendChild(
          $xml_doc->createElement('recipientFName', $address['first_name'])
        );

        $linexml->appendChild(
          $xml_doc->createElement('recipientLName', $address['last_name'])
        );

        // Middle name not used by default but could be added using a custom
        // module hook.
        $linexml->appendChild(
          $xml_doc->createElement('recipientMName', '')
        );

        // Salutation not used by default but could be added using a custom
        // module hook.
        $linexml->appendChild(
          $xml_doc->createElement('recipientSal', '')
        );

        // Email address specific to order not used by default but could be added
        // using a custom module hook.
        $linexml->appendChild(
          $xml_doc->createElement('recipientEmail', '')
        );

        // Assuming there is a field called 'field_telephone' on the address which
        // there isn't by default. If an alternative name field has been used, a
        // custom module hook can be used to populate this.
        $telephone = '';
        if (isset($shipping_profile->field_telephone)) {
          $telephone = $shipping_profile->field_telephone->value();
        }

        $linexml->appendChild(
          $xml_doc->createElement('recipientPhone', $telephone)
        );

        $address = $shipping_profile->commerce_customer_address->value();
        $linexml->appendChild(
          $xml_doc->createElement('receipentAdd1', $address['thoroughfare'] . ' ' . $address['premise'])
        );

        $linexml->appendChild(
          $xml_doc->createElement('recipientCity', $address['locality'])
        );

        $linexml->appendChild(
          $xml_doc->createElement('recipientCountry', $address['country'])
        );

        $linexml->appendChild(
          $xml_doc->createElement('recipientPostCode', $address['postal_code'])
        );
      }

    }


    // Add the delivery totals if we found a delivery line earlier.

    if (isset($delivery_line)) {
      // Step through components to look for tax item
      $delivery_tax = 0;
      // check if there is a tax component.
      $delivery_total = $delivery_line->commerce_total->value();

      if (isset($delivery_total['data']['components'])) {
        foreach ($delivery_total['data']['components'] as $c) {
          if (strpos($c['name'], 'tax') === 0) {
            $delivery_tax += $c['price']['amount'] / 100;
          }
        }
      }

      // Add Delivery info.
      $root->appendChild(
        $xml_doc->createElement("deliveryNetAmount", $delivery_total['data']['components'][0]['price']['amount']/100)
      );

      $root->appendChild(
        $xml_doc->createElement("deliveryTaxAmount", $delivery_tax)
      );

      $root->appendChild(
        $xml_doc->createElement("deliveryGrossAmount", $delivery_total['amount']/100)
      );

      $root->appendChild(
        $xml_doc->createElement("shipId", '')
      );
      $root->appendChild(
        $xml_doc->createElement("shippingMethod", $delivery_line->commerce_shipping_service->value())
      );
    }
    $root->appendChild(
      $xml_doc->createElement("totalNumItems", $total_items)
    );

    /*
    * The remainder of this specification is industry specific (eg airlines / hotels)
    * Adding a hook here so the XML can be enhanced when industry specific sites have this information
    * to add. No point in creating a generic version here as we don't know what the fields used will be called.
    */
    drupal_alter('commerce_sagepay_basket_xml', $xml_doc);

    $xml_doc->formatOutput = FALSE;
      //@todo currently SagePay is rejecting this XML as invalid - need to find out why.
    return $xml_doc;
  }

  /**
   * Convert a an Order object into a string in the format required by
   * SagePay protocol 3.0.
   * @param $order the order to be converted.
   * @return string the order as a string.
   */
  function _commerce_sagepay_cart_to_string($order) {
    $order_string = '';

    // Load an array of line items from the order.
    $order_lines = $order->commerce_line_items['und'];

    // Determine the total number of lines - this is the first component of the string we send to SagePay.
    $total_lines = sizeof($order_lines);
    $order_string = $total_lines . ':';

    // Encode each order line and add to the string.
    for ($line = 0; $line < $total_lines; $line++) {
      $order_string .= _commerce_sagepay_line_item_to_string($order_lines[$line]['line_item_id']);
    }

    // Trim off the last excess colon
    $order_string = substr($order_string, 0, strlen($order_string) -1);

    return $order_string;
  }

  /**
   * Convert a single order line into a string in the format requried by SagePay.
   * @param $line_item_id the id of the line item to be converted.
   * @return string the line item as a string.
   */
  function _commerce_sagepay_line_item_to_string($line_item_id) {

    // Load the line item ID
    $line_item = commerce_line_item_load($line_item_id);
    $line_item_wrapper = entity_metadata_wrapper('commerce_line_item', $line_item);


    $description = $line_item_wrapper->line_item_label->value();

    // remove any colons from the line description as we need this as a delimiter.
    $description = str_ireplace(':', ' ', $description);

    $quantity = $line_item_wrapper->quantity->value();

    // Get the price components for the order line.
    $item_value = $line_item_wrapper->commerce_unit_price->value();

    // Step through components to look for tax item
    $line_data = $line_item_wrapper->commerce_unit_price->value();

    // Set a default value for the tax component in case the site is not using tax.
    $item_tax = array('amount' => 0, 'currency_code' => $item_value['currency_code']);
    
    foreach ($line_data['data']['components'] as $c) {
      if (strpos($c['name'], 'tax') === 0) {
        $item_tax = commerce_price_component_total($item_value, $c['name']);
      }
    }

    $total = $line_item_wrapper->commerce_total->value();

    $fl_item_total = commerce_currency_amount_to_decimal($item_value['amount'], $item_value['currency_code']);
    $fl_tax_value = commerce_currency_amount_to_decimal($item_tax['amount'], $item_tax['currency_code']);
    $fl_item_value = $fl_item_total - $fl_tax_value;
    $fl_total_value = $fl_item_total * $quantity;

    $line_as_text = $description . ':' . $quantity . ':' . number_format($fl_item_value, 2) . ':' . number_format($fl_tax_value, 2) . ':' . number_format($fl_item_total, 2) . ':' . number_format($fl_total_value, 2) . ':';

    return $line_as_text;
  }
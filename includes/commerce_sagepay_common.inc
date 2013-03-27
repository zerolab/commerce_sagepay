<?php
  /**
   * @file
   * Common utility functions shared by all SagePay modules.
   */

  function commerce_sagepay_order_form($form, &$form_state, $order, $settings) {

    // Wrap the order for easier access to data.
    $wrapper = entity_metadata_wrapper('commerce_order', $order);

    $total = commerce_line_items_total($wrapper->commerce_line_items);

    // Add tax if we have sales tax in the order.
    $total['amount'] = $wrapper->commerce_order_total->amount->value();

    // Load customer profile.
    $profile = commerce_customer_profile_load($order->commerce_customer_billing[LANGUAGE_NONE][0]['profile_id']);

    // Get user billing address.
    $billing_address = $profile->commerce_customer_address[LANGUAGE_NONE][0];

    // Get user delivery address.
    $delivery_address = NULL;

    if (isset($order->commerce_customer_shipping)) {
      $delivery_profile = commerce_customer_profile_load($order->commerce_customer_shipping[LANGUAGE_NONE][0]['profile_id']);
      $delivery_address = $delivery_profile->commerce_customer_address[LANGUAGE_NONE][0];
    }

    // Encrypt the order details (address and amount) ready to send to SagePay.
    $encrypted_order = _commerce_sagepay_encrypted_order($settings, $order, $total, $billing_address, $delivery_address);

    // Determine the correct transaction type based on the payment gateway settings.
    switch (variable_get(SAGEPAY_SETTING_TRANSACTION_TYPE)) {
      case COMMERCE_CREDIT_AUTH_CAPTURE:
        $tx_type = 'PAYMENT';
        break;
      case COMMERCE_CREDIT_AUTH_ONLY:
        $tx_type = 'DEFERRED';
        break;
      default:
        // Set to deferred by default if there is no setting for the payment gateway.
        $tx_type = 'DEFERRED';
    }

    // Build the data array that will be translated into hidden form values.
    $data = array(
      'VPSProtocol' => SAGEPAY_PROTOCOL,
      'TxType' => $tx_type,
      'Vendor' => variable_get(SAGEPAY_SETTING_VENDOR_NAME),
      'Crypt' => $encrypted_order,
    );

    // determine the correct url based on the transaction mode.
    switch (variable_get(SAGEPAY_SETTING_TRANSACTION_MODE)) {
      case SAGEPAY_TXN_MODE_LIVE:
        $server_url = SAGEPAY_FORM_SERVER_LIVE;
        break;
      case SAGEPAY_TXN_MODE_TEST:
        $server_url = SAGEPAY_FORM_SERVER_TEST;
        break;

      case SAGEPAY_TXN_MODE_SIMULATION:
        $server_url = SAGEPAY_FORM_SERVER_SIMULATION;
        break;

    }

    $form['#action'] = $server_url;

    foreach ($data as $name => $value) {
      if (!empty($value)) {
        $form[$name] = array('#type' => 'hidden', '#value' => $value);
      }
    }

    $form['submit'] = array(
      '#type' => 'submit',
      '#value' => t('Proceed to SagePay'),
    );

    return $form;
  }




  /**
   * Create a Transaction and associate it with the order.
   * @param $payment_method
   * @param $order
   * @param $charge
   * @param $tokens
   * @param $transaction_status
   * @param $remote_status
   * @param $transaction
   */
  function commerce_sagepay_transaction($payment_method, $order, $charge, $tokens, $transaction_status, $remote_status, $transaction = NULL) {
    $tokens['VendorTxCode'] = $order->data['VendorTxId'];

    if (!isset($transaction)) {
      $transaction = commerce_payment_transaction_new($payment_method['method_id'], $order->order_id);
      $transaction->instance_id = $payment_method['instance_id'];
      $transaction->amount = $charge['amount'];
      $transaction->currency_code = $charge['currency_code'];

    }
    $transaction->payload += $tokens;
    if (array_key_exists('VPSTxId', $tokens)) $transaction->remote_id = $tokens['VPSTxId'];
    $transaction->remote_status = $remote_status;
    // set a status for the payment - one of COMMERCE_PAYMENT_STATUS_SUCCESS, COMMERCE_PAYMENT_STATUS_PENDING or COMMERCE_PAYMENT_STATUS_FAILURE
    $transaction->status = $transaction_status;

    $transaction_message = 'Status @status, @statusdetail. ';
    $transaction_message .= 'VPSTxId=@vpstxid. ';
    $transaction_message .= 'Auth Code=@authcode. ';
    $transaction_message .= 'Address Check: @address. ';
    $transaction_message .= 'Postcode Check: @postcode. ';
    $transaction_message .= 'AVSCV2 Result: @avs. ';
    $transaction_message .= '3D Secure: @tds. ';
    $transaction_message .= 'Address Status: @addressstatus. ';
    $transaction_message .= 'Payer Status: @payerstatus. ';
    $transaction_message .= 'Card Type: @cardtype. ';
    $transaction_message .= 'last 4 Digits: @last4digits. ';
    $transaction_message .= 'Fraud Response: @fraudresponse. ';
    $transaction_message .= 'Surcharge: @surcharge. ';
    $transaction_message .= 'Bank Auth Code: @bankauthcode. ';
    $transaction_message .= 'Decline Code: @declinecode. ';

    $transaction->message = $transaction_message;
    $transaction->message_variables =
      array(
        '@status' => $tokens['Status'],
        '@statusdetail' => $tokens['StatusDetail'],
        '@vpstxid' => isset($tokens['VPSTxId']) ? $tokens['VPSTxId'] : 'N/A',
        '@authcode' => isset($tokens['TxAuthNo']) ? $tokens['TxAuthNo'] : 'N/A',
        '@address' => isset($tokens['AddressResult']) ? $tokens['AddressResult'] : 'N/A',
        '@postcode' => isset($tokens['PostCodeResult']) ? $tokens['PostCodeResult'] : 'N/A',
        '@avs' => isset($tokens['AVSCV2']) ? $tokens['AVSCV2'] : 'N/A',
        '@tds' => isset($tokens['3DSecureStatus']) ? $tokens['3DSecureStatus'] : 'N/A',
        '@giftaid' => isset($tokens['GiftAid']) ? $tokens['GiftAid'] : 'No',
        '@addressstatus' => isset($tokens['AddressStatus']) ? $tokens['AddressStatus'] : 'N/A',
        '@payerstatus' => isset($tokens['PayerStatus']) ? $tokens['PayerStatus'] : 'N/A',
        '@cardtype' => isset($tokens['CardType']) ? $tokens['CardType'] : 'N/A',
        '@last4digits' => isset($tokens['Last4Digits']) ? $tokens['Last4Digits'] : 'N/A',
        '@fraudresponse' => isset($tokens['FraudResponse']) ? $tokens['FraudResponse'] : 'N/A',
        '@surcharge' => isset($tokens['Surcharge']) ? $tokens['Surcharge'] : 'N/A',
        '@bankauthcode' => isset($tokens['BankAuthCode']) ? $tokens['BankAuthCode'] : 'N/A',
        '@declinecode' => isset($tokens['DeclineCode']) ? $tokens['DeclineCode'] : 'N/A',
      );
    commerce_payment_transaction_save($transaction);

  }

  /**
   * Helper function to process the response from SagePay of any transaction type.
   * Return TRUE for a successful transaction.
   * @param $payment_method
   * @param $order
   * @param $tokens
   * @return bool
   */
  function commerce_sagepay_process_response($payment_method, $order, $tokens) {
    // default no charge for failed transactions
    // (these values will not be in the callback for a failed or cancelled transaction)
    $def_charge = array('amount' => 0, 'currency_code' => '');
    $order_id = $order->order_id;
    // Check for a valid status callback.
    switch ($tokens['Status']) {
      case 'ABORT' :
        watchdog('commerce_sagepay', 'ABORT error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ALERT);
        commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
        drupal_set_message(t('Your SagePay transaction was aborted.'), 'error');
        return FALSE;
      case 'NOTAUTHED' :
        watchdog('commerce_sagepay', 'NOTAUTHED error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ALERT);
        commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
        drupal_set_message(t('Your transaction was not authorised.'), 'error');
        return FALSE;
      case 'REJECTED' :
        watchdog('commerce_sagepay', 'REJECTED error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ALERT);
        commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
        drupal_set_message(t('Your transaction was rejected.'), 'error');
        return FALSE;
      case 'MALFORMED' :
        watchdog('commerce_sagepay', 'MALFORMED error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ALERT);
        commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
        drupal_set_message(t('Sorry the transaction has failed.'), 'error');
        return FALSE;
      case 'INVALID' :
        watchdog('commerce_sagepay', 'INVALID error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ERROR);
        commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
        drupal_set_message(t('Sorry an error occurred while processing your transaction. %message', array('%message' => $tokens['StatusDetail'])), 'error');
        return FALSE;
      case 'ERROR' :
        watchdog('commerce_sagepay', 'System ERROR from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ERROR);
        commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
        drupal_set_message(t('Sorry an error occurred while processing your transaction. %message', array('%message' => $tokens['StatusDetail'])), 'error');
        return FALSE;
      case 'OK' :
        break;
      case 'AUTHENTICATED' :
        watchdog('commerce_sagepay', 'AUTHENTICATED Payment callback received from SagePay for order %order_id with status code %status', array('%order_id' => $order_id, '%status' => $tokens['Status']));
        break;
      case 'REGISTERED' :
        watchdog('commerce_sagepay', 'REGISTERED Payment callback received from SagePay for order %order_id with status code %status', array('%order_id' => $order_id, '%status' => $tokens['Status']));
        break;
      case '3DAUTH' :
        // Server has replied with a 3D Secure authentication request.
          // Store the returned variables in the order object for processing
          // by the 3D Secure module.
          // The returned variables should be:
          // 1) PAReq
          // 2) ACSURL
          // 3) MD
        $tds_data = array();
        $tds_data['PAReq'] = $tokens['PAReq'];
        $tds_data['ACSURL'] = $tokens['ACSURL'];
        $tds_data['MD'] = $tokens['MD'];
        $tds_data['TermUrl'] = url('commerce-sagepay/3d_secure_callback/' . $order->order_number , array('absolute' => TRUE));
        $order->data['extra_authorisation'] = $tds_data;

        break;
      default:
        // If the status code is anything other than those above, something has gone wrong so log an error and fail.
        watchdog('commerce_sagepay', 'Unrecognised Status response from SagePay for order %order_id (%response_code)', array('%order_id' => $order_id, '%response_code' => $tokens['Status']), WATCHDOG_ERROR);
        drupal_set_message(t('Sorry an error occurred while processing your transaction. %message', array('%message' => $tokens['StatusDetail'])), 'error');
        return FALSE;

    }

    // Validation successful.
    // Create a transaction and associate it with the order.
    // If teh CAVV token is present, we must be coming back from
    // 3D Secure authentication so we need to load the original transaction to complete it.
    if (array_key_exists('CAVV', $tokens)) {
      // Load the transaction from the original order so we can get the details we need.
      $conditions = array('order_id' => $order->order_id, 'remote_status' => SAGEPAY_REMOTE_STATUS_3D_SECURE);
      $transactions = commerce_payment_transaction_load_multiple(array(), $conditions);
      $transaction = array_pop($transactions);
      $arr_charge = array();
      $arr_charge['amount'] = $transaction->amount;
      $arr_charge['currency_code'] = $transaction->currency_code;

    }
    else {
      $arr_charge = array();
      $order_wrapper = entity_metadata_wrapper('commerce_order', $order);
      $arr_charge['amount'] = $tokens['Amount'];
      $arr_charge['currency_code'] = $order_wrapper->commerce_order_total->currency_code->value();
      $transaction = NULL;
    }
    switch ($tokens['Status']) {
      case 'OK':
        commerce_sagepay_transaction($payment_method, $order, $arr_charge, $tokens, COMMERCE_PAYMENT_STATUS_SUCCESS, SAGEPAY_REMOTE_STATUS_PAYMENT, $transaction);

        break;
      case 'REGISTERED':
        commerce_sagepay_transaction($payment_method, $order, $arr_charge, $tokens, COMMERCE_PAYMENT_STATUS_PENDING, SAGEPAY_REMOTE_STATUS_REGISTERED);
        break;
      case 'AUTHENTICATED':
        commerce_sagepay_transaction($payment_method, $order, $arr_charge, $tokens, COMMERCE_PAYMENT_STATUS_PENDING, SAGEPAY_REMOTE_STATUS_AUTHENTICATE);
        break;
      case '3DAUTH':
        commerce_sagepay_transaction($payment_method, $order, $arr_charge, $tokens, COMMERCE_PAYMENT_STATUS_PENDING, SAGEPAY_REMOTE_STATUS_3D_SECURE);
      break;
    }
    return TRUE;

  }




  /**
   * Encrypt the order details ready to send to SagePay Server
   **/
  function _commerce_sagepay_encrypted_order($settings, $order, $total, $billing_address, $delivery_address = NULL, $integration_method = SAGEPAY_FORM, $pane_values = NULL) {

    // Check for Commerce Card on File value which indicates someone is trying
    // to reuse a stored payment card.
    // Redirect the request to the sagepay_token module
    if (module_exists('sagepay_token')) {
      if (isset($pane_values) && array_key_exists('cardonfile', $pane_values) && $pane_values['cardonfile'] != 'new') {
        $saved_card_data = commerce_cardonfile_data_load($pane_values['cardonfile']);
        if (!commerce_cardonfile_data_access($saved_card_data)) $saved_card_data = NULL;
        if (isset($saved_card_data)) $integration_method = SAGEPAY_TOKEN;
      }
    }

    // add a random number to the transaction ID so that the order can be resubmitted to SagePage in the
    // event the user clicks back to modify the order before completing. (otherwise we SagePay rejects this
    // as a duplicate)
    $random_number = rand(0, 32000) * rand(0, 32000);
    $vendor_tx_code = $order->order_number . "_" . $random_number;

    // Update order number in database with the random number we just generated.
    $order->data['VendorTxId'] = $vendor_tx_code;
    commerce_order_save($order);

    // set up an array of tokens to encrypt ready for SagePay


    // convert commerce int to decimal
    $order_amount = number_format(commerce_currency_amount_to_decimal($total['amount'], $total['currency_code']), 2);

    // Check if we need to encode cart.
    $cart_setting = variable_get('sagepay_send_basket_contents');
    $encoded_cart = '';
    if ($cart_setting == '1') {
      module_load_include('inc', 'commerce_sagepay', 'includes/commerce_sagepay_formatters');
      $encoded_cart = _commerce_sagepay_cart_to_string($order);
    }
    elseif ($cart_setting == '2') {
      module_load_include('inc', 'commerce_sagepay', 'includes/commerce_sagepay_formatters');
      $encoded_cart = _commerce_sagepay_cart_to_xml($order)->saveXML();
    }

    $billing_name = _commerce_sagepay_format_customer_name(
      $billing_address['name_line'],
      $billing_address['first_name'],
      $billing_address['last_name']
    );

    // Add a default postcode if the address supplied didn't have one.
    // SagePay requires a postcode even if some countries do not have them.
    if (!isset($billing_address['postal_code'])) {
      $billing_address['postal_code'] = '0000';
    }

    // Determine the correct transaction type based on the payment gateway settings.
    switch (variable_get(SAGEPAY_SETTING_TRANSACTION_TYPE)) {
      case COMMERCE_CREDIT_AUTH_CAPTURE:
        $tx_type = 'PAYMENT';
        break;
      case COMMERCE_CREDIT_AUTH_ONLY:
        $tx_type = 'AUTHENTICATE';
        break;
      default:
        // Set to deferred by default if there is no setting for the payment gateway.
        $tx_type = 'DEFERRED';
    }



    $query = array(
      'VPSProtocol' => SAGEPAY_PROTOCOL,
      'Vendor' => variable_get(SAGEPAY_SETTING_VENDOR_NAME),
      'VendorTxCode' => $vendor_tx_code,
//      'TxType' => $tx_type,
      'Amount' => $order_amount,
      'Currency' => $total['currency_code'],
      'Description' => variable_get(SAGEPAY_SETTING_ORDER_DESCRIPTION),
      'CustomerName' => $billing_name['first_name'] . ' ' . $billing_name['last_name'],
      'CustomerEmail' => $order->mail,
      'VendorEmail' => variable_get(SAGEPAY_SETTING_VENDOR_EMAIL),
      'SendEmail' => variable_get(SAGEPAY_SETTING_SEND_EMAIL),
      'eMailMessage' => variable_get(SAGEPAY_SETTING_EMAIL_MESSAGE),
      'BillingSurname' => $billing_name['last_name'],
      'BillingFirstnames' => $billing_name['first_name'],
      'BillingAddress1' => $billing_address['thoroughfare'],
      'BillingAddress2' => $billing_address['premise'],
      'BillingCity' => $billing_address['locality'],
      'BillingPostcode' => $billing_address['postal_code'],
      'BillingCountry' => $billing_address['country'],
      // 'BillingPhone' => $billing_address['postal_code'],
      'DeliverySurname' => $billing_name['last_name'],
      'DeliveryFirstnames' => $billing_name['first_name'],
      'DeliveryAddress1' => $billing_address['thoroughfare'],
      'DeliveryAddress2' => $billing_address['premise'],
      'DeliveryCity' => $billing_address['locality'],
      'DeliveryPostcode' => $billing_address['postal_code'],
      'DeliveryCountry' => $billing_address['country'],
      // 'DeliveryState' => $billing_address[''],
      // 'DeliveryPhone' => $billing_address[''],
      // 'AllowGiftAid' => '',
      'ApplyAVSCV2' => variable_get(SAGEPAY_SETTING_APPLY_AVS_CV2),
      'Apply3DSecure' => variable_get(SAGEPAY_SETTING_APPLY_3D_SECURE),
      // 'BillingAgreement' => '',

    );


    switch ($integration_method) {
      case SAGEPAY_FORM :
        $query['SuccessURL'] = isset($settings['return']) ? $settings['return'] : '';
        $query['FailureURL'] = isset($settings['cancel_return']) ? $settings['cancel_return'] : '';
        break;
      case SAGEPAY_SERVER :
        $query['NotificationURL'] = url('commerce-sagepay-server/vps-callback/' . $order->order_number . '/' . $order->data['payment_redirect_key'], array('absolute' => TRUE));

        $query['TxType'] = $tx_type;
        if (variable_get(SAGEPAY_SETTING_LOW_PROFILE, 1) == 1) {
          $query['Profile'] = 'LOW';
        }
        else {
          $query['Profile'] = 'NORMAL';
        }
        break;


      case SAGEPAY_DIRECT :
        $query['TxType'] = $tx_type;
        $query['CardHolder'] = $billing_name['first_name'] . ' ' . $billing_name['last_name'];
        $query['CardNumber'] = $pane_values['credit_card']['number'];
        $query['ExpiryDate'] = $pane_values['credit_card']['exp_month'] . substr($pane_values['credit_card']['exp_year'], 2, 2);
        $query['StartDate'] = $pane_values['credit_card']['start_month'] . substr($pane_values['credit_card']['start_year'], 2, 2);
        $query['IssueNumber'] = $pane_values['credit_card']['issue'];
        $query['CV2'] = $pane_values['credit_card']['code'];
        $query['CardType'] = $pane_values['credit_card']['type'];

        // Add 3D Secure flag only if the 3d Secure module is enabled for DIRECT
        if (module_exists('sagepay_3d_secure')) {
          $query['Apply3DSecure'] = variable_get(SAGEPAY_SETTING_APPLY_3D_SECURE);
        }
        else {
          $query['Apply3DSecure'] = 2;
        }
        break;

      case SAGEPAY_TOKEN :
//        $query['TxType'] = 'TOKEN';
        $query['TxType'] = $tx_type;

        $query['Token'] = $saved_card_data['remote_id'];
        $query['StoreToken'] = 1; //@todo decide when to set this to zero - on failed transaction probably.
        $query['ApplyAVSCV2'] = 2; // Disable CV2 check as we do not store this data in Card on File
        // option to add CV2 here, but card on file doesn't store it.
    }


    switch ($cart_setting) {
      case 1:
        $query['Basket'] = $encoded_cart;
      break;

      case 2:
        $query['BasketXML'] = $encoded_cart;
      break;
    }

    // Add check for state for US addresses only.
    if ($billing_address['country'] == 'US') {
      $query['BillingState'] = $billing_address['administrative_area'];
      $query['DeliveryState'] = $billing_address['administrative_area'];
    }

    // Override with supplied delivery address if we have one.
    if (isset($delivery_address)) {

      $delivery_name = _commerce_sagepay_format_customer_name(
        isset($delivery_address['name_line']) ? $delivery_address['name_line'] : NULL,
        isset($delivery_address['first_name']) ? $delivery_address['first_name'] : NULL,
        isset($delivery_address['last_name']) ? $delivery_address['last_name'] : NULL
      );
      $query['DeliverySurname'] = $delivery_name['last_name'];
      $query['DeliveryFirstnames'] = $delivery_name['first_name'];
      $query['DeliveryAddress1'] = $delivery_address['thoroughfare'];
      $query['DeliveryAddress2'] = $delivery_address['premise'];
      $query['DeliveryCity'] = $delivery_address['locality'];
      $query['DeliveryPostcode'] = $delivery_address['postal_code'];
      $query['DeliveryCountry'] = $delivery_address['country'];
      // 'DeliveryPhone' = $delivery_address[''];

      if ($delivery_address['country'] == 'US' && $delivery_address['administrative_area']) {
        $query['DeliveryState'] = $delivery_address['administrative_area'];
      }
    }

    /*
     * Call hook to allow other modules to modify order data before it is encrypted or used.
     */
    $query['integration_method'] = $integration_method;
    drupal_alter('sagepay_order_data', $query, $order);
    unset($query['integration_method']);

    // For Server mode, we can return the Array now before encryption.
    if (in_array($integration_method, array(SAGEPAY_SERVER, SAGEPAY_DIRECT, SAGEPAY_TOKEN))) return $query;



    $keys = array_keys($query);
    $query_string = '';
    foreach ($keys as $key) {
      $query_string .= $key . '=' . $query[$key] . '&';
    }
    $query_string = substr($query_string, 0, strlen($query_string) -1);

    // Encrypt the order details using base64 and the secret key from the settings.
    switch(variable_get(SAGEPAY_SETTING_TRANSACTION_MODE)){
      case SAGEPAY_TXN_MODE_LIVE:
        $encoded_string =  _commerce_sagepay_encrypt_and_encode($query_string, variable_get(SAGEPAY_SETTING_ENCRYPTION_KEY));
        break;
      default :
        $encoded_string =  _commerce_sagepay_encrypt_and_encode($query_string, variable_get(SAGEPAY_SETTING_TEST_ENCRYPTION_KEY));
    }


    return $encoded_string;
  }


  /**
   * Helper function for SagePay Form integration that converts a string of tokens
   * into an array.
   * @param $tokenizedstring
   * @return array
   */
  function _commerce_sagepay_form_get_tokens($tokenizedstring) {

    // List the possible tokens.
    $tokens = array(
      'Status',
      'StatusDetail',
      'VendorTxCode',
      'VPSTxId',
      'TxAuthNo',
      'Amount',
      'AVSCV2',
      'AddressResult',
      'PostCodeResult',
      'CV2Result',
      'GiftAid',
      '3DSecureStatus',
      'CAVV',
      'AddressStatus',
      'PayerStatus',
      'CardType',
      'Last4Digits',
      'FraudResponse',
      'Surcharge',
      'BankAuthCode',
      'DeclineCode',

    );

    // Initialise arrays.
    $output = array();
    $result = array();

    // Get the next token in the sequence.
    for ($i = count($tokens)-1; $i >= 0 ; $i--) {
      // Find the position in the string.
      $start = strpos($tokenizedstring, $tokens[$i]);
      // If it's present
      if ($start !== FALSE) {
        // Record position and token name.
        $result[$i] = new stdClass();
        $result[$i]->start = $start;
        $result[$i]->token = $tokens[$i];
      }
    }

    // Sort in order of position.
    sort($result);

    // Go through the result array, getting the token values.
    for ($i = 0; $i<count($result); $i++) {
      // Get the start point of the value.
      $value_start = $result[$i]->start + strlen($result[$i]->token) + 1;
      // Get the length of the value.
      if ($i==(count($result)-1)) {
        $output[$result[$i]->token] = substr($tokenizedstring, $value_start);
      }
      else {
        $value_length = $result[$i+1]->start - $result[$i]->start - strlen($result[$i]->token) - 2;
        $output[$result[$i]->token] = substr($tokenizedstring, $value_start, $value_length);
      }

    }

    // Return the ouput array.
    return $output;
  }


  /**
   * Uses CURL to send a POST request to SagePay and return the response as an array.
   * @param $url
   * @param $data
   * @return array
   */
  function _commerce_sagepay_request_post($url, $data) {
    // Set a one-minute timeout for this script
    set_time_limit(60);

    // Initialise output variable
    $output = array();

    // Open the cURL session

    $curl_session = curl_init();

    // Set the URL
    curl_setopt($curl_session, CURLOPT_URL, $url);
    // No headers, please
    curl_setopt($curl_session, CURLOPT_HEADER, 0);
    // It's a POST request
    curl_setopt($curl_session, CURLOPT_POST, 1);
    // Set the fields for the POST
    curl_setopt($curl_session, CURLOPT_POSTFIELDS, $data);
    // Return it direct, don't print it out
    curl_setopt($curl_session, CURLOPT_RETURNTRANSFER, 1);
    // This connection will timeout in 30 seconds
    curl_setopt($curl_session, CURLOPT_TIMEOUT, 30);
    //The next two lines must be present for the kit to work with newer version of cURL
    //You should remove them if you have any problems in earlier versions of cURL
    curl_setopt($curl_session, CURLOPT_SSL_VERIFYPEER, FALSE);
    curl_setopt($curl_session, CURLOPT_SSL_VERIFYHOST, 1);

    //Send the request and store the result in an array

    $rawresponse = curl_exec($curl_session);
    //Store the raw response for later as it's useful to see for integration and understanding

    //Split response into name=value pairs
    $response = explode(chr(10), $rawresponse);
    // Check that a connection was made
    if (curl_error($curl_session)) {
      // If it wasn't...
      $output['Status'] = "FAIL";
      $output['StatusDetail'] = curl_error($curl_session);
    }

    // Close the cURL session
    curl_close($curl_session);

    // Tokenise the response
    for ($i=0; $i<count($response); $i++) {
      // Find position of first "=" character
      $split_at = strpos($response[$i], "=");
      // Create an associative (hash) array with key/value pairs ('trim' strips excess whitespace)
      $output[trim(substr($response[$i], 0, $split_at))] = trim(substr($response[$i], ($split_at+1)));
    } // END for ($i=0; $i<count($response); $i++)

    // Return the output
    return $output;
  }
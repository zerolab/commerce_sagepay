<?php

/**
 * @file
 * commerce_sagepay_server.inc
 * Functions for Server integration method.
 */

module_load_include('inc', 'commerce_sagepay', 'includes/commerce_sagepay_common');
module_load_include('inc', 'commerce_sagepay', 'includes/commerce_sagepay_utils');


/**
 * Implements hook_redirect_form().
 */
function commerce_sagepay_server_redirect_form($form, &$form_state, $order, $payment_method) {
  // Return an error if the enabling action's settings haven't been configured.
  if (!variable_get(SAGEPAY_SETTING_VENDOR_NAME)) {
    drupal_set_message(t("SagePay Server Integration is not configured for use.
      <a href=\"/admin/commerce/config/sagepay\">Set a vendor name on the settings page.</a>"),
      'error');

    return array();
  }

  // Add Javascript to allow iFrame to work.
  drupal_add_js(drupal_get_path('module', 'commerce_sagepay') . '/js/commerce_sagepay_server.js');

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

  $settings = array();
  $data = _commerce_sagepay_encrypted_order($settings, $order, $total, $billing_address, $delivery_address, SAGEPAY_SERVER);

  // Create a POST to send to SagePay.
  $post = '';
  foreach ($data as $name => $value) {
    $post .= urlencode($name) . '=' . urlencode($value) . '&';
  }
  // Chop off the last &.
  $post = substr($post, 0, -1);

  // Determine the correct url based on the transaction mode.
  switch (variable_get(SAGEPAY_SETTING_TRANSACTION_MODE)) {
    case SAGEPAY_TXN_MODE_LIVE:
      $server_url = SAGEPAY_SERVER_SERVER_LIVE;
      break;

    case SAGEPAY_TXN_MODE_TEST:
      $server_url = SAGEPAY_SERVER_SERVER_TEST;
      break;

    case SAGEPAY_TXN_MODE_SIMULATION:
      $server_url = SAGEPAY_SERVER_SERVER_SIMULATION;
      break;
  }

  // Call SagePay to get payment form that can then be included in an iFrame.
  // Response as per Appendix A2 of the SagePay Server Integration Guidelines.
  $response = _commerce_sagepay_request_post($server_url, $post);

  // Create a new payment transaction and setup the amount.
  $transaction = commerce_payment_transaction_new('commerce_sagepay_server', $order->order_id);
  $transaction->amount = $total['amount'];
  $transaction->currency_code = $total['currency_code'];
  $transaction->status = COMMERCE_PAYMENT_STATUS_PENDING;
  $transaction->instance_id = $payment_method['instance_id'];
  $transaction->remote_id = isset($response['VPSTxId']) ? $response['VPSTxId'] : '';
  $transaction->payload = $response;
  $transaction->payload['VendorTxCode'] = $data['VendorTxCode'];

  // Add the user id to the transaction.
  $transaction->payload['merchantCustomerId'] = $order->uid;

  // Determine the next action depending on the Status Code returned.
  if (!isset($response['Status'])) {
    $transaction->payload['vps_status'] = 'TIMEOUT';
    $transaction->status = COMMERCE_PAYMENT_STATUS_FAILURE;
    $transaction->message = 'No valid response from Sagepay';
    commerce_payment_transaction_save($transaction);
    watchdog('custom_sagepay_server', 'No Status code received in SagePay callback.', array(), WATCHDOG_ERROR);
  }

  $transaction_status = explode(" ", $response["Status"]);
  switch ($transaction_status[0]) {
    case 'OK':
      $transaction->remote_status = SAGEPAY_REMOTE_STATUS_STARTED;
      commerce_payment_transaction_save($transaction);

      $iframe_enabled = variable_get(SAGEPAY_SETTING_USE_IFRAME, 0);

      if ($iframe_enabled == 1) {
        // Build the iFrame for the next step.
        $form['#action'] = $response["NextURL"];
        $form['#attributes']['target'] = 'iframe_sagepay';

        $form['submit'] = array(
          '#type'  => 'submit',
          '#value' => t('Proceed to SagePay'),
        );
        $form['iframe_sagepay'] = array(
          '#markup' => '<iframe style="width: 600px; height:500px" scrolling="no" frameborder="0" name="iframe_sagepay" id="iframe_sagepay" src="' . url('commerce_sagepay/sagepay_waiting_page') . '" />',
        );

        return $form;
      }
      else {
        header("Location: " . $response["NextURL"]);
      }

      exit;

    case 'FAIL':
      $transaction->payload['vps_status'] = 'FAIL';
      $transaction->remote_status = SAGEPAY_REMOTE_STATUS_FAIL;
      $transaction->status = COMMERCE_PAYMENT_STATUS_FAILURE;
      $transaction->message = 'FAIL Response from Sagepay';
      commerce_payment_transaction_save($transaction);
      drupal_goto('checkout/' . $order->order_id . '/payment/return/' . $order->data['payment_redirect_key']);
      break;

    case 'INVALID':
      $transaction->payload['vps_status'] = 'INVALID';
      $transaction->remote_status = SAGEPAY_REMOTE_STATUS_INVALID;
      $transaction->status = COMMERCE_PAYMENT_STATUS_FAILURE;
      $transaction->message = $response['StatusDetail'];
      commerce_payment_transaction_save($transaction);
      watchdog('commerce_sagepay', 'INVALID Status response from SagePay for order %order_id', array('%order_id' => $order->order_id), WATCHDOG_ERROR);
      drupal_goto('checkout/' . $order->order_id . '/payment/return/' . $order->data['payment_redirect_key']);
      break;

    default:
      $transaction->payload['vps_status'] = 'UNKNOWN';
      $transaction->remote_status = SAGEPAY_REMOTE_STATUS_UNKNOWN;
      $transaction->status = COMMERCE_PAYMENT_STATUS_FAILURE;
      $transaction->message = 'Unknown or invalid response from Sagepay';
      commerce_payment_transaction_save($transaction);
      watchdog('commerce_sagepay', 'Unrecognised Status response from SagePay for order %order_id (%response_code)', array('%order_id' => $order->order_id, '%response_code' => $transaction_status[0]), WATCHDOG_ERROR);
      drupal_goto('checkout/' . $order->order_id . '/payment/return/' . $order->data['payment_redirect_key']);
      break;
  }

}


/**
 * Validate payment by checking status of last transaction.
 *
 * @param commerce_order $order
 *  The Commerce Order.
 * @param array $payment_method
 *  The payment method.
 * @return bool
 *  Return TRUE if the form validates.
 */
function commerce_sagepay_server_redirect_form_validate($order, $payment_method) {
  $return = FALSE;
  $transactions = commerce_payment_transaction_load_multiple(array(), array('order_id' => $order->order_id));
  if (count($transactions) > 0) {
    $transaction = end($transactions);
    $transaction->status = COMMERCE_PAYMENT_STATUS_FAILURE;

    $failure_message = t('There was a problem processing your transaction. Your credit/debit card was not charged. Please try again later.');
    switch ($transaction->payload['vps_status']) {
      case 'OK':
        $transaction->status = COMMERCE_PAYMENT_STATUS_SUCCESS;
        $return = TRUE;
        break;

      case 'TAMPER':
        drupal_set_message(check_plain($failure_message), 'error');
        break;

      case 'NOTAUTHED';
        drupal_set_message(check_plain($failure_message), 'error');
        break;

      case 'REJECTED':
        drupal_set_message(check_plain($failure_message), 'error');
        break;

      case 'ABORT':
        break;

      case 'ERROR':
      case 'FAIL':
      case 'FAIL2':
      case 'UNKNOWN':
        drupal_set_message(check_plain($failure_message), 'error');
        break;
    }
    commerce_payment_transaction_save($transaction);
  }

  return $return;
}


/**
 * Outputs a temporary page before the SagePay form is submitted.
 *
 * Displayed when there is a load delay or if the user has Javascript disabled.
 */
function commerce_sagepay_server_waiting_page() {
  print ('<html><head><title></title></head><body><p>');
  print t('Please wait to be redirected to the payment page.');
  print ('</p></body></html>');
}

/**
 * Process the callback that is sent by SagePay Server.
 *
 * @param string $order_id
 *    The order id.
 * @param string $key
 *    The security key.
 */
function commerce_sagepay_server_handle_callback($order_id, $key) {
  $reject_notification = FALSE;

  $order = commerce_order_load($order_id);

  // Check key against supplied value.
  if (!$order->data['payment_redirect_key'] === $key) {
    $reject_notification = TRUE;
    $notification_status = 'INVALID';
    $notification_status_detail = t('Payment Redirect key did not match.');
  }

  if (empty($_POST)) {
    $reject_notification = TRUE;
    $notification_status = 'ERROR';
    $notification_status_detail = t('No Payload returned in the notification POST.');
  }
  if (!$reject_notification) {
    $transactions = commerce_payment_transaction_load_multiple(array(), array('order_id' => $order->order_id));
    foreach ($transactions as $transaction) {
      if ($transaction->payload['VendorTxCode'] == $_POST["VendorTxCode"]) {
        // Check we have the correct transaction.
        $transaction->payload = $transaction->payload + $_POST;
        $payment_method = $transaction->payment_method;

        // Check for tampering.
        // Empty as we already know the amount in this scenario.
        $charge = array();

        $md5_check = array();

        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["VPSTxId"]) ? $_REQUEST["VPSTxId"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["VendorTxCode"]) ? $_REQUEST["VendorTxCode"] : '', "VendorTxCode");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["Status"]) ? $_REQUEST["Status"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["TxAuthNo"]) ? $_REQUEST["TxAuthNo"] : '', "Number");
        $md5_check[] = variable_get(SAGEPAY_SETTING_VENDOR_NAME);
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["AVSCV2"]) ? $_REQUEST["AVSCV2"] : '', "Text");
        $md5_check[] = $transaction->payload['SecurityKey'];
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["AddressResult"]) ? $_REQUEST["AddressResult"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["PostCodeResult"]) ? $_REQUEST["PostCodeResult"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["CV2Result"]) ? $_REQUEST["CV2Result"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["GiftAid"]) ? $_REQUEST["GiftAid"] : '', "Number");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["3DSecureStatus"]) ? $_REQUEST["3DSecureStatus"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["CAVV"]) ? $_REQUEST["CAVV"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["AddressStatus"]) ? $_REQUEST["AddressStatus"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["PayerStatus"]) ? $_REQUEST['PayerStatus'] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["CardType"]) ? $_REQUEST["CardType"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["Last4Digits"]) ? $_REQUEST["Last4Digits"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["DeclineCode"]) ? $_REQUEST["DeclineCode"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["ExpiryDate"]) ? $_REQUEST["ExpiryDate"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["FraudResponse"]) ? $_REQUEST["FraudResponse"] : '', "Text");
        $md5_check[] = _commerce_sagepay_clean_input(isset($_REQUEST["BankAuthCode"]) ? $_REQUEST["BankAuthCode"] : '', "Text");

        $str_vpssignature = _commerce_sagepay_clean_input($_REQUEST["VPSSignature"], "Text");
        $str_message = implode('', $md5_check);
        $str_mysignature = strtoupper(md5($str_message));

        if ($str_mysignature == $str_vpssignature) {
          $transaction->payload['vps_status'] = $_POST['Status'];
          switch ($_POST['Status']) {
            case 'OK':
              watchdog('commerce_sagepay', 'OK Payment callback received from SagePay for order %order_id with status code %status', array('%order_id' => $order_id, '%status' => $transaction->payload['Status']));
              commerce_sagepay_transaction($payment_method, $order, $charge, $transaction->payload, COMMERCE_PAYMENT_STATUS_SUCCESS, SAGEPAY_REMOTE_STATUS_PAYMENT, $transaction);
              break;

            case 'NOTAUTHED':
              watchdog('commerce_sagepay', 'NOTAUTHED error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $transaction->payload['StatusDetail']), WATCHDOG_ALERT);
              commerce_sagepay_transaction($payment_method, $order, $charge, $transaction->payload, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL, $transaction);
              break;

            case 'REJECTED':
              watchdog('commerce_sagepay', 'REJECTED error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $transaction->payload['StatusDetail']), WATCHDOG_ALERT);
              commerce_sagepay_transaction($payment_method, $order, $charge, $transaction->payload, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL, $transaction);
              break;

            case 'ABORT':
              watchdog('commerce_sagepay', 'ABORT error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $transaction->payload['StatusDetail']), WATCHDOG_ALERT);
              commerce_sagepay_transaction($payment_method, $order, $charge, $transaction->payload, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL, $transaction);
              break;

            case 'FAIL':
              watchdog('commerce_sagepay', 'FAIL error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $transaction->payload['StatusDetail']), WATCHDOG_ERROR);
              commerce_sagepay_transaction($payment_method, $order, $charge, $transaction->payload, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL, $transaction);
              break;

          }
          $notification_status = 'OK';
          $notification_status_detail = t('Transaction notification received.');

        }
        else {
          $transaction->payload['vps_status'] = 'TAMPER';
          $transaction->remote_status = 'TAMPER';
          $transaction->status = COMMERCE_PAYMENT_STATUS_FAILURE;
          commerce_sagepay_transaction($payment_method, $order, array(), $transaction->payload, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL, $transaction);
          $notification_status = 'INVALID';
          $notification_status_detail = t('MD5 did not match - signs of tampering.');
        }

        commerce_payment_transaction_save($transaction);
        break;

      }
    }
  }

  // Send response back to SagePay to indicate we have received and processed.
  $eoln = chr(13) . chr(10);
  $notification_redirect_url = url('checkout/' . $order->order_id . '/payment/return/' . $order->data['payment_redirect_key'], array('absolute' => TRUE));

  $return_notification = 'Status=' . $notification_status . $eoln . 'RedirectURL=' . $notification_redirect_url . $eoln . 'StatusDetail=' . $notification_status_detail . $eoln;

  echo $return_notification;
  exit;

}

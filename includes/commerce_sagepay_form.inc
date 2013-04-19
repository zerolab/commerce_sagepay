<?php

/**
 * @file
 * commerce_sagepay_form.inc
 *
 * Form integration functions.
 */
module_load_include('inc', 'commerce_sagepay', 'includes/commerce_sagepay_common');
module_load_include('inc', 'commerce_sagepay', 'includes/commerce_sagepay_utils');

/**
 * Implements hook_form_redirect_form().
 */
function commerce_sagepay_form_redirect_form($form, &$form_state, $order,
                                              $payment_method) {
  // Return an error if the enabling action's settings haven't been configured.
  if (!variable_get(SAGEPAY_SETTING_VENDOR_NAME)) {
    drupal_set_message(t("SagePay Form Integration is not configured for use.
      <a href=\"/admin/commerce/config/sagepay\">Set a vendor name on the settings page.</a>"),
      'error');

    return array();
  }

  if (!variable_get(SAGEPAY_SETTING_ENCRYPTION_KEY)) {
    drupal_set_message(t('SagePay Form Integration is not configured for use.
      <a href=\"/admin/commerce/config/sagepay\">Set an encryption key on the settings page.</a>'),
      'error');

    return array();
  }

  $settings = array(
    // Return to the payment redirect on failure/cancellation.
    // so we can handle it and give user feedback
    'cancel_return'  => url('checkout/' . $order->order_id . '/payment/return/' . $order->data['payment_redirect_key'], array('absolute' => TRUE)),

    // Return to the payment redirect page for processing successful payments.
    'return'         => url('checkout/' . $order->order_id . '/payment/return/' . $order->data['payment_redirect_key'], array('absolute' => TRUE)),

    // Specify the current payment method instance ID in the notify_url.
    'payment_method' => $payment_method['instance_id'],
  );

  return commerce_sagepay_order_form($form, $form_state, $order, $settings);
}

/**
 * Implements hook_redirect_form_validate().
 */
function commerce_sagepay_form_redirect_form_validate($order, $payment_method) {
  // SagePay sends a callback to the site as a single encrypted string.
  // This is appened to the success or failure URL.
  $encrypted_response = $_GET['crypt'];

  // Now we have the encrypted response, we need to decrypt this using the same
  // secret key that we used to send the request in the first place.
  // The secret key is stored in the payment method.
  $enc_key = variable_get(SAGEPAY_SETTING_ENCRYPTION_KEY);
  if (!isset($enc_key)) {
    watchdog('commerce_sagepay_form', 'Cannot load SagePay key from payment method in order to decrypt response', array(), WATCHDOG_ERROR);

    return FALSE;
  }

  // Decrypt the response received from SagePay.
  $unencrypted_response = _commerce_sagepay_decode_and_decrypt($encrypted_response, $enc_key);

  // Split the decrypted string into an array of tokens.
  $tokens = _commerce_sagepay_form_get_tokens($unencrypted_response);

  // Split the vendor code to get rid of the random number that was added.
  $arrtmp = explode('_', $tokens['VendorTxCode']);
  $order_id = $arrtmp[0];


  // Validate the returned decrypted data.
  if ($order_id != $order->order_id) {
    watchdog('commerce_sagepay_form', 'Returned order id does not match order for this session', array(), WATCHDOG_ERROR);

    return FALSE;
  }

  // Default no charge for failed transactions.
  $def_charge = array('amount' => 0, 'currency_code' => '');

  // Check for a valid status callback.
  switch ($tokens['Status']) {
    case 'ABORT':
      watchdog('commerce_sagepay', 'ABORT error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ALERT);
      commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
      drupal_set_message(t('Your SagePay transaction was aborted.'), 'error');
      return FALSE;

    case 'NOTAUTHED':
      watchdog('commerce_sagepay', 'NOTAUTHED error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ALERT);
      commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
      drupal_set_message(t('Your transaction was not authorised by SagePay'), 'error');
      return FALSE;

    case 'REJECTED':
      watchdog('commerce_sagepay', 'REJECTED error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ALERT);
      commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
      drupal_set_message(t('Your transaction was rejected by SagePay'), 'error');
      return FALSE;

    case 'MALFORMED':
      watchdog('commerce_sagepay', 'MALFORMED error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ALERT);
      commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
      drupal_set_message(t('Sorry the transaction has failed.'), 'error');
      return FALSE;

    case 'INVALID':
      watchdog('commerce_sagepay', 'INVALID error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ERROR);
      commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
      drupal_set_message(t('Sorry the transaction has failed.'), 'error');
      return FALSE;

    case 'ERROR':
      watchdog('commerce_sagepay', 'System ERROR from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ERROR);
      commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
      drupal_set_message(t('Sorry an error occurred while processing your transaction.'), 'error');
      return FALSE;

    case 'OK':
      watchdog('commerce_sagepay', 'OK Payment callback received from SagePay for order %order_id with status code %status', array('%order_id' => $order_id, '%status' => $tokens['Status']));
      $remote_status = SAGEPAY_REMOTE_STATUS_OK;
      break;

    case 'AUTHENTICATED':
      watchdog('commerce_sagepay', 'AUTHENTICATED Payment callback received from SagePay for order %order_id with status code %status', array('%order_id' => $order_id, '%status' => $tokens['Status']));
      $remote_status = SAGEPAY_REMOTE_STATUS_AUTHENTICATE;
      break;

    case 'REGISTERED':
      watchdog('commerce_sagepay', 'REGISTERED Payment callback received from SagePay for order %order_id with status code %status', array('%order_id' => $order_id, '%status' => $tokens['Status']));
      $remote_status = SAGEPAY_REMOTE_STATUS_REGISTERED;
      break;

    default:
      // If the status code is anything other than those above, something has gone wrong so log an error and fail.
      watchdog('commerce_sagepay', 'Unrecognised Status response from SagePay for order %order_id (%response_code)', array('%order_id' => $order_id, '%response_code' => $tokens['Status']), WATCHDOG_ERROR);
      return FALSE;

  }

  // Validation successful.
  // Create a transaction and associate it with the order.
  $charge = array();
  $charge['amount'] = $tokens['Amount'] * 100; // convert back to commerce int
  $charge['currency_code'] = $order->commerce_order_total['und'][0]['currency_code'];

  commerce_sagepay_transaction($payment_method, $order, $charge, $tokens, $tokens['Status'], $remote_status);

  return TRUE;
}

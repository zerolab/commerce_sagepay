<?php

/**
 * @file commerce_sagepay_form.inc
 * Form integration functions.
 */
module_load_include('inc', 'commerce_sagepay', 'includes/commerce_sagepay_common');
module_load_include('inc', 'commerce_sagepay', 'includes/commerce_sagepay_utils');


/**
 * Implements hook_redirect_form.
 **/
function commerce_sagepay_form_redirect_form($form, &$form_state, $order, $payment_method) {
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
    //Return to the payment redirect on failure/cancellation
    //so we can handle it and give user feedback
    'cancel_return' => url('checkout/' . $order->order_id . '/payment/return/' . $order->data['payment_redirect_key'], array('absolute' => TRUE)),

    // Return to the payment redirect page for processing successful payments
    'return' => url('checkout/' . $order->order_id . '/payment/return/' . $order->data['payment_redirect_key'], array('absolute' => TRUE)),

    // Specify the current payment method instance ID in the notify_url
    'payment_method' => $payment_method['instance_id'],
  );

  return commerce_sagepay_order_form($form, $form_state, $order, $settings);


}

/**
 * Implements hook_redirect_form_validate
 */
function commerce_sagepay_form_redirect_form_validate($order, $payment_method) {
  // SagePay sends a callback to the site as a single encrypted string called Crypt
  // this is appened to the success or failure URL.
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

  // The response we get back will have the following tokens available:
  // Status = a String containing the status of the transaction from the possible values:
  // ABORT, NOTAUTHED, REJECTED, MALFORMED, INVALID, ERROR, OK, AUTHENTICATED, REGISTERED

  // StatusDetail = Human-readable text providing extra detail for the Status message

  // VendorTxCode = The order ID we send with the original request. This can be used to apply this transaction back to the order.

  // VPSTxId = The unique transaction ID created by SagePay

  // TxAuthNo = The transaction authorisation number

  // Amount = The amount that was taken in the payment (this should match the amount requested and can be used to verify the call back)

  // AVSCV2 = The result of the card AVSCV2 security check
  // This will be one of ALL MATCH, SECURITY CODE MATCH ONLY, ADDRESS MATCH ONLY, NO DATA MATCHES or DATA NOT CHECKED

  // AddressResult = The result of the address check (comparing the card address entered by the user to the one on the credit card record at the provider)
  // This will be one of: NOTPROVIDED, NOTCHECKED, MATCHED, NOTMATCHED

  // PostCodeResult = The result of the postcode check (comparing the card address postcode entered by the user to the one on the credit card record at the provider)
  // This will be one of NOTPROVIDED, NOTCHECKED, MATCHED, NOTMATCHED

  // CV2Result = The results of the CV2 security check
  // This will be one off NOTPROVIDED, NOTCHECKED, MATCHED, NOTMATCHED

  // GiftAid = Whether or not the user specified Gift Aid during the transaction (charity payments only)
  // This will be either: 0 = The Gift Aid box was not checked this transaction. 1 = The user checked the Gift Aid box on the payment page

  // 3DSecureStatus: This will be one of:
  // OK - 3D Secure checks carried out and user authenticated correctly.
  // NOTCHECKED - 3D-Secure checks were not performed. NOTAVAILABLE - The card used was either not part of the
  // 3D Secure Scheme, or the authorisation was not possible.
  // NOTAUTHED - 3D-Secure authentication checked, but the user failed the authentication.
  // INCOMPLETE - 3D-Secure authentication was unable to complete. No authentication occurred.
  // ERROR - Authentication could not be attempted due to data errors or service unavailability in one of the parties involved in the check.

  // CAVV = The encoded result code from the 3D-Secure checks (CAVV or UCAF)."CAVV" - Only present if the 3DSecureStatus is OK

  // CardType =  The card type used to complete the transaction. This will be one of:
  // VISA, MC, DELTA, SOLO, MAESTRO, UKE, AMEX, DC, JCB, LASER, PAYPAL

  // Last4Digits = The last 4 digits of the card number used in this transaction. PayPal transactions have 0000

  // Split the decrypted string into an array of tokens.
  $tokens = _commerce_sagepay_form_get_tokens($unencrypted_response);

  // Split the vendor code to get rid of the random number that was added to the end before sending.
  $arrtmp = explode('_', $tokens['VendorTxCode']);
  $order_id = $arrtmp[0];


  // Validate the returned decrypted data
  if ($order_id != $order->order_id) {
    watchdog('commerce_sagepay_form', 'Returned order id does not match order for this session', array(), WATCHDOG_ERROR);
    return FALSE;
  }

  // default no charge for failed transactions
  // (these values will not be in the callback for a failed or cancelled transaction)
  $def_charge = array('amount' => 0, 'currency_code' => '');

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
      drupal_set_message(t('Your transaction was not authorised by SagePay'), 'error');
      return FALSE;
    case 'REJECTED' :
      watchdog('commerce_sagepay', 'REJECTED error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ALERT);
      commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
      drupal_set_message(t('Your transaction was rejected by SagePay'), 'error');
      return FALSE;
    case 'MALFORMED' :
      watchdog('commerce_sagepay', 'MALFORMED error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ALERT);
      commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
      drupal_set_message(t('Sorry the transaction has failed.'), 'error');
      return FALSE;
    case 'INVALID' :
      watchdog('commerce_sagepay', 'INVALID error from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ERROR);
      commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
      drupal_set_message(t('Sorry the transaction has failed.'), 'error');
      return FALSE;
    case 'ERROR' :
      watchdog('commerce_sagepay', 'System ERROR from SagePay for order %order_id with message %msg', array('%order_id' => $order_id, '%msg' => $tokens['StatusDetail']), WATCHDOG_ERROR);
      commerce_sagepay_transaction($payment_method, $order, $def_charge, $tokens, COMMERCE_PAYMENT_STATUS_FAILURE, SAGEPAY_REMOTE_STATUS_FAIL);
      drupal_set_message(t('Sorry an error occurred while processing your transaction.'), 'error');
      return FALSE;
    case 'OK' :
      watchdog('commerce_sagepay', 'OK Payment callback received from SagePay for order %order_id with status code %status', array('%order_id' => $order_id, '%status' => $tokens['Status']));
      $remote_status = SAGEPAY_REMOTE_STATUS_OK;
      break;
    case 'AUTHENTICATED' :
      watchdog('commerce_sagepay', 'AUTHENTICATED Payment callback received from SagePay for order %order_id with status code %status', array('%order_id' => $order_id, '%status' => $tokens['Status']));
      $remote_status = SAGEPAY_REMOTE_STATUS_AUTHENTICATE;
      break;
    case 'REGISTERED' :
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
//    function commerce_sagepay_transaction($payment_method, $order, $charge, $tokens, $transaction_status) {


  $charge = array();
  $charge['amount'] = $tokens['Amount'] * 100; // convert back to commerce int
  $charge['currency_code'] = $order->commerce_order_total['und'][0]['currency_code'];

  commerce_sagepay_transaction($payment_method, $order, $charge, $tokens, $tokens['Status'], $remote_status);

  return TRUE;
}



